// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ranges>

#include "common/config.h"
#include "common/io_file.h"
#include "common/path_util.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/info.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

extern std::unique_ptr<Vulkan::RendererVulkan> renderer;

namespace Vulkan {

using Shader::VsOutput;

[[nodiscard]] inline u64 HashCombine(const u64 seed, const u64 hash) {
    return seed ^ (hash + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

void GatherVertexOutputs(Shader::VertexRuntimeInfo& info,
                         const AmdGpu::Liverpool::VsOutputControl& ctl) {
    const auto add_output = [&](VsOutput x, VsOutput y, VsOutput z, VsOutput w) {
        if (x != VsOutput::None || y != VsOutput::None || z != VsOutput::None ||
            w != VsOutput::None) {
            info.outputs.emplace_back(Shader::VsOutputMap{x, y, z, w});
        }
    };
    // VS_OUT_MISC_VEC
    add_output(ctl.use_vtx_point_size ? VsOutput::PointSprite : VsOutput::None,
               ctl.use_vtx_edge_flag
                   ? VsOutput::EdgeFlag
                   : (ctl.use_vtx_gs_cut_flag ? VsOutput::GsCutFlag : VsOutput::None),
               ctl.use_vtx_kill_flag
                   ? VsOutput::KillFlag
                   : (ctl.use_vtx_render_target_idx ? VsOutput::GsMrtIndex : VsOutput::None),
               ctl.use_vtx_viewport_idx ? VsOutput::GsVpIndex : VsOutput::None);
    // VS_OUT_CCDIST0
    add_output(ctl.IsClipDistEnabled(0)
                   ? VsOutput::ClipDist0
                   : (ctl.IsCullDistEnabled(0) ? VsOutput::CullDist0 : VsOutput::None),
               ctl.IsClipDistEnabled(1)
                   ? VsOutput::ClipDist1
                   : (ctl.IsCullDistEnabled(1) ? VsOutput::CullDist1 : VsOutput::None),
               ctl.IsClipDistEnabled(2)
                   ? VsOutput::ClipDist2
                   : (ctl.IsCullDistEnabled(2) ? VsOutput::CullDist2 : VsOutput::None),
               ctl.IsClipDistEnabled(3)
                   ? VsOutput::ClipDist3
                   : (ctl.IsCullDistEnabled(3) ? VsOutput::CullDist3 : VsOutput::None));
    // VS_OUT_CCDIST1
    add_output(ctl.IsClipDistEnabled(4)
                   ? VsOutput::ClipDist4
                   : (ctl.IsCullDistEnabled(4) ? VsOutput::CullDist4 : VsOutput::None),
               ctl.IsClipDistEnabled(5)
                   ? VsOutput::ClipDist5
                   : (ctl.IsCullDistEnabled(5) ? VsOutput::CullDist5 : VsOutput::None),
               ctl.IsClipDistEnabled(6)
                   ? VsOutput::ClipDist6
                   : (ctl.IsCullDistEnabled(6) ? VsOutput::CullDist6 : VsOutput::None),
               ctl.IsClipDistEnabled(7)
                   ? VsOutput::ClipDist7
                   : (ctl.IsCullDistEnabled(7) ? VsOutput::CullDist7 : VsOutput::None));
}

Shader::RuntimeInfo PipelineCache::BuildRuntimeInfo(Shader::Stage stage) {
    auto info = Shader::RuntimeInfo{stage};
    const auto& regs = liverpool->regs;
    switch (stage) {
    case Shader::Stage::Vertex: {
        info.num_user_data = regs.vs_program.settings.num_user_regs;
        info.num_input_vgprs = regs.vs_program.settings.vgpr_comp_cnt;
        // vgprs allocated in granularity of 4
        info.num_allocated_vgprs = regs.vs_program.settings.num_vgprs * 4;
        GatherVertexOutputs(info.vs_info, regs.vs_output_control);
        info.vs_info.emulate_depth_negative_one_to_one =
            !instance.IsDepthClipControlSupported() &&
            regs.clipper_control.clip_space == Liverpool::ClipSpace::MinusWToW;
        break;
    }
    case Shader::Stage::Fragment: {
        info.num_user_data = regs.ps_program.settings.num_user_regs;
        info.num_allocated_vgprs = regs.ps_program.settings.num_vgprs * 4;
        std::ranges::transform(graphics_key.mrt_swizzles, info.fs_info.mrt_swizzles.begin(),
                               [](Liverpool::ColorBuffer::SwapMode mode) {
                                   return static_cast<Shader::MrtSwizzle>(mode);
                               });
        const auto& ps_inputs = regs.ps_inputs;
        for (u32 i = 0; i < regs.num_interp; i++) {
            info.fs_info.inputs.push_back({
                .param_index = u8(ps_inputs[i].input_offset.Value()),
                .is_default = bool(ps_inputs[i].use_default),
                .is_flat = bool(ps_inputs[i].flat_shade),
                .default_value = u8(ps_inputs[i].default_value),
            });
        }
        break;
    }
    case Shader::Stage::Compute: {
        const auto& cs_pgm = regs.cs_program;
        info.num_user_data = cs_pgm.settings.num_user_regs;
        info.num_allocated_vgprs = cs_pgm.settings.num_vgprs * 4;
        info.cs_info.workgroup_size = {cs_pgm.num_thread_x.full, cs_pgm.num_thread_y.full,
                                       cs_pgm.num_thread_z.full};
        info.cs_info.tgid_enable = {cs_pgm.IsTgidEnabled(0), cs_pgm.IsTgidEnabled(1),
                                    cs_pgm.IsTgidEnabled(2)};
        info.cs_info.shared_memory_size = cs_pgm.SharedMemSize();
        break;
    }
    default:
        break;
    }
    return info;
}

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             AmdGpu::Liverpool* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, liverpool{liverpool_} {
    profile = Shader::Profile{
        .supported_spirv = instance.ApiVersion() >= VK_API_VERSION_1_3 ? 0x00010600U : 0x00010500U,
        .subgroup_size = instance.SubgroupSize(),
        .support_explicit_workgroup_layout = true,
    };
    pipeline_cache = instance.GetDevice().createPipelineCacheUnique({});
}

PipelineCache::~PipelineCache() = default;

const GraphicsPipeline* PipelineCache::GetGraphicsPipeline() {
    const auto& regs = liverpool->regs;
    // Tessellation is unsupported so skip the draw to avoid locking up the driver.
    if (regs.primitive_type == Liverpool::PrimitiveType::PatchPrimitive) {
        return nullptr;
    }
    // There are several cases (e.g. FCE, FMask/HTile decompression) where we don't need to do an
    // actual draw hence can skip pipeline creation.
    if (regs.color_control.mode == Liverpool::ColorControl::OperationMode::EliminateFastClear) {
        LOG_TRACE(Render_Vulkan, "FCE pass skipped");
        return nullptr;
    }
    if (regs.color_control.mode == Liverpool::ColorControl::OperationMode::FmaskDecompress) {
        // TODO: check for a valid MRT1 to promote the draw to the resolve pass.
        LOG_TRACE(Render_Vulkan, "FMask decompression pass skipped");
        return nullptr;
    }
    if (!RefreshGraphicsKey()) {
        return nullptr;
    }
    const auto [it, is_new] = graphics_pipelines.try_emplace(graphics_key);
    if (is_new) {
        it.value() = std::make_unique<GraphicsPipeline>(instance, scheduler, graphics_key,
                                                        *pipeline_cache, infos, modules);
    }
    const GraphicsPipeline* pipeline = it->second.get();
    return pipeline;
}

const ComputePipeline* PipelineCache::GetComputePipeline() {
    if (!RefreshComputeKey()) {
        return nullptr;
    }
    const auto [it, is_new] = compute_pipelines.try_emplace(compute_key);
    if (is_new) {
        it.value() = std::make_unique<ComputePipeline>(instance, scheduler, *pipeline_cache,
                                                       compute_key, *infos[0], modules[0]);
    }
    const ComputePipeline* pipeline = it->second.get();
    return pipeline;
}

bool ShouldSkipShader(u64 shader_hash, const char* shader_type) {
    static constexpr std::array<u64, 0> skip_hashes = {};
    if (std::ranges::contains(skip_hashes, shader_hash)) {
        LOG_WARNING(Render_Vulkan, "Skipped {} shader hash {:#x}.", shader_type, shader_hash);
        return true;
    }
    return false;
}

bool PipelineCache::RefreshGraphicsKey() {
    auto& regs = liverpool->regs;
    auto& key = graphics_key;

    key.depth = regs.depth_control;
    key.depth.depth_write_enable.Assign(regs.depth_control.depth_write_enable.Value() &&
                                        !regs.depth_render_control.depth_clear_enable);
    key.depth_bounds_min = regs.depth_bounds_min;
    key.depth_bounds_max = regs.depth_bounds_max;
    key.depth_bias_enable = regs.polygon_control.enable_polygon_offset_back ||
                            regs.polygon_control.enable_polygon_offset_front ||
                            regs.polygon_control.enable_polygon_offset_para;
    if (regs.polygon_control.enable_polygon_offset_front) {
        key.depth_bias_const_factor = regs.poly_offset.front_offset;
        key.depth_bias_slope_factor = regs.poly_offset.front_scale;
    } else {
        key.depth_bias_const_factor = regs.poly_offset.back_offset;
        key.depth_bias_slope_factor = regs.poly_offset.back_scale;
    }
    key.depth_bias_clamp = regs.poly_offset.depth_bias;
    key.stencil = regs.stencil_control;
    key.stencil_ref_front = regs.stencil_ref_front;
    key.stencil_ref_back = regs.stencil_ref_back;
    key.prim_type = regs.primitive_type;
    key.enable_primitive_restart = regs.enable_primitive_restart & 1;
    key.primitive_restart_index = regs.primitive_restart_index;
    key.polygon_mode = regs.polygon_control.PolyMode();
    key.cull_mode = regs.polygon_control.CullingMode();
    key.clip_space = regs.clipper_control.clip_space;
    key.front_face = regs.polygon_control.front_face;
    key.num_samples = regs.aa_config.NumSamples();

    const auto& db = regs.depth_buffer;
    const auto ds_format = LiverpoolToVK::DepthFormat(db.z_info.format, db.stencil_info.format);

    if (db.z_info.format != AmdGpu::Liverpool::DepthBuffer::ZFormat::Invalid) {
        key.depth_format = ds_format;
    } else {
        key.depth_format = vk::Format::eUndefined;
    }
    if (key.depth.depth_enable) {
        key.depth.depth_enable.Assign(key.depth_format != vk::Format::eUndefined);
    }

    if (db.stencil_info.format != AmdGpu::Liverpool::DepthBuffer::StencilFormat::Invalid) {
        key.stencil_format = key.depth_format;
    } else {
        key.stencil_format = vk::Format::eUndefined;
    }
    if (key.depth.stencil_enable) {
        key.depth.stencil_enable.Assign(key.stencil_format != vk::Format::eUndefined);
    }

    const auto skip_cb_binding =
        regs.color_control.mode == AmdGpu::Liverpool::ColorControl::OperationMode::Disable;

    // `RenderingInfo` is assumed to be initialized with a contiguous array of valid color
    // attachments. This might be not a case as HW color buffers can be bound in an arbitrary order.
    // We need to do some arrays compaction at this stage
    key.color_formats.fill(vk::Format::eUndefined);
    key.blend_controls.fill({});
    key.write_masks.fill({});
    key.mrt_swizzles.fill(Liverpool::ColorBuffer::SwapMode::Standard);
    int remapped_cb{};
    for (auto cb = 0u; cb < Liverpool::NumColorBuffers; ++cb) {
        auto const& col_buf = regs.color_buffers[cb];
        if (skip_cb_binding || !col_buf || !regs.color_target_mask.GetMask(cb)) {
            continue;
        }
        const auto base_format =
            LiverpoolToVK::SurfaceFormat(col_buf.info.format, col_buf.NumFormat());
        const bool is_vo_surface = renderer->IsVideoOutSurface(col_buf);
        key.color_formats[remapped_cb] = LiverpoolToVK::AdjustColorBufferFormat(
            base_format, col_buf.info.comp_swap.Value(), false /*is_vo_surface*/);
        if (base_format == key.color_formats[remapped_cb]) {
            key.mrt_swizzles[remapped_cb] = col_buf.info.comp_swap.Value();
        }
        key.blend_controls[remapped_cb] = regs.blend_control[cb];
        key.blend_controls[remapped_cb].enable.Assign(key.blend_controls[remapped_cb].enable &&
                                                      !col_buf.info.blend_bypass);
        key.write_masks[remapped_cb] = vk::ColorComponentFlags{regs.color_target_mask.GetMask(cb)};
        key.cb_shader_mask = regs.color_shader_mask;

        ++remapped_cb;
    }

    u32 binding{};
    for (u32 i = 0; i < MaxShaderStages; i++) {
        if (!regs.stage_enable.IsStageEnabled(i)) {
            key.stage_hashes[i] = 0;
            infos[i] = nullptr;
            continue;
        }
        auto* pgm = regs.ProgramForStage(i);
        if (!pgm || !pgm->Address<u32*>()) {
            key.stage_hashes[i] = 0;
            infos[i] = nullptr;
            continue;
        }
        const auto* bininfo = Liverpool::GetBinaryInfo(*pgm);
        if (!bininfo->Valid()) {
            LOG_WARNING(Render_Vulkan, "Invalid binary info structure!");
            key.stage_hashes[i] = 0;
            infos[i] = nullptr;
            continue;
        }
        if (ShouldSkipShader(bininfo->shader_hash, "graphics")) {
            return false;
        }
        const auto stage = Shader::StageFromIndex(i);
        const auto params = Liverpool::GetParams(*pgm);
        std::tie(infos[i], modules[i], key.stage_hashes[i]) = GetProgram(stage, params, binding);
    }
    return true;
}

bool PipelineCache::RefreshComputeKey() {
    u32 binding{};
    const auto* cs_pgm = &liverpool->regs.cs_program;
    const auto cs_params = Liverpool::GetParams(*cs_pgm);
    if (ShouldSkipShader(cs_params.hash, "compute")) {
        return false;
    }
    std::tie(infos[0], modules[0], compute_key) =
        GetProgram(Shader::Stage::Compute, cs_params, binding);
    return true;
}

vk::ShaderModule PipelineCache::CompileModule(Shader::Info& info,
                                              const Shader::RuntimeInfo& runtime_info,
                                              std::span<const u32> code, size_t perm_idx,
                                              u32& binding) {
    LOG_INFO(Render_Vulkan, "Compiling {} shader {:#x} {}", info.stage, info.pgm_hash,
             perm_idx != 0 ? "(permutation)" : "");
    if (Config::dumpShaders()) {
        DumpShader(code, info.pgm_hash, info.stage, perm_idx, "bin");
    }

    const auto ir_program = Shader::TranslateProgram(code, pools, info, runtime_info, profile);
    const auto spv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime_info, ir_program, binding);
    if (Config::dumpShaders()) {
        DumpShader(spv, info.pgm_hash, info.stage, perm_idx, "spv");
    }

    const auto module = CompileSPV(spv, instance.GetDevice());
    const auto name = fmt::format("{}_{:#x}_{}", info.stage, info.pgm_hash, perm_idx);
    Vulkan::SetObjectName(instance.GetDevice(), module, name);
    return module;
}

std::tuple<const Shader::Info*, vk::ShaderModule, u64> PipelineCache::GetProgram(
    Shader::Stage stage, Shader::ShaderParams params, u32& binding) {
    const auto runtime_info = BuildRuntimeInfo(stage);
    auto [it_pgm, new_program] = program_cache.try_emplace(params.hash);
    if (new_program) {
        Program* program = program_pool.Create(stage, params);
        u32 start_binding = binding;
        program->info.num_allocated_vgprs = runtime_info.num_allocated_vgprs;
        const auto module = CompileModule(program->info, runtime_info, params.code, 0, binding);
        const auto spec = Shader::StageSpecialization(program->info, runtime_info, start_binding);
        program->AddPermut(module, std::move(spec));
        it_pgm.value() = program;
        return std::make_tuple(&program->info, module, HashCombine(params.hash, 0));
    }

    Program* program = it_pgm->second;
    const auto& info = program->info;
    const auto spec = Shader::StageSpecialization(info, runtime_info, binding);
    size_t perm_idx = program->modules.size();
    vk::ShaderModule module{};

    const auto it = std::ranges::find(program->modules, spec, &Program::Module::spec);
    if (it == program->modules.end()) {
        auto new_info = Shader::Info(stage, params);
        module = CompileModule(new_info, runtime_info, params.code, perm_idx, binding);
        program->AddPermut(module, std::move(spec));
    } else {
        binding += info.NumBindings();
        module = it->module;
        perm_idx = std::distance(program->modules.begin(), it);
    }
    return std::make_tuple(&info, module, HashCombine(params.hash, perm_idx));
}

void PipelineCache::DumpShader(std::span<const u32> code, u64 hash, Shader::Stage stage,
                               size_t perm_idx, std::string_view ext) {
    using namespace Common::FS;
    const auto dump_dir = GetUserPath(PathType::ShaderDir) / "dumps";
    if (!std::filesystem::exists(dump_dir)) {
        std::filesystem::create_directories(dump_dir);
    }
    const auto filename = fmt::format("{}_{:#018x}_{}.{}", stage, hash, perm_idx, ext);
    const auto file = IOFile{dump_dir / filename, FileAccessMode::Write};
    file.WriteSpan(code);
}

} // namespace Vulkan
