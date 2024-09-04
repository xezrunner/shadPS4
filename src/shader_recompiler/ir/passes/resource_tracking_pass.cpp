// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <boost/container/small_vector.hpp>
#include "common/alignment.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/breadth_first_search.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/resource.h"

namespace Shader::Optimization {
namespace {

struct SharpLocation {
    u32 sgpr_base;
    u32 dword_offset;

    auto operator<=>(const SharpLocation&) const = default;
};

bool IsBufferAtomic(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::BufferAtomicIAdd32:
    case IR::Opcode::BufferAtomicSMin32:
    case IR::Opcode::BufferAtomicUMin32:
    case IR::Opcode::BufferAtomicSMax32:
    case IR::Opcode::BufferAtomicUMax32:
    case IR::Opcode::BufferAtomicInc32:
    case IR::Opcode::BufferAtomicDec32:
    case IR::Opcode::BufferAtomicAnd32:
    case IR::Opcode::BufferAtomicOr32:
    case IR::Opcode::BufferAtomicXor32:
    case IR::Opcode::BufferAtomicSwap32:
        return true;
    default:
        return false;
    }
}

bool IsBufferStore(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::StoreBufferF32:
    case IR::Opcode::StoreBufferF32x2:
    case IR::Opcode::StoreBufferF32x3:
    case IR::Opcode::StoreBufferF32x4:
    case IR::Opcode::StoreBufferU32:
        return true;
    default:
        return IsBufferAtomic(inst);
    }
}

bool IsBufferInstruction(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadBufferF32:
    case IR::Opcode::LoadBufferF32x2:
    case IR::Opcode::LoadBufferF32x3:
    case IR::Opcode::LoadBufferF32x4:
    case IR::Opcode::LoadBufferU32:
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::ReadConstBufferU32:
        return true;
    default:
        return IsBufferStore(inst);
    }
}

bool IsTextureBufferInstruction(const IR::Inst& inst) {
    return inst.GetOpcode() == IR::Opcode::LoadBufferFormatF32 ||
           inst.GetOpcode() == IR::Opcode::StoreBufferFormatF32;
}

static bool UseFP16(AmdGpu::DataFormat data_format, AmdGpu::NumberFormat num_format) {
    switch (num_format) {
    case AmdGpu::NumberFormat::Float:
        switch (data_format) {
        case AmdGpu::DataFormat::Format16:
        case AmdGpu::DataFormat::Format16_16:
        case AmdGpu::DataFormat::Format16_16_16_16:
            return true;
        default:
            return false;
        }
    case AmdGpu::NumberFormat::Unorm:
    case AmdGpu::NumberFormat::Snorm:
    case AmdGpu::NumberFormat::Uscaled:
    case AmdGpu::NumberFormat::Sscaled:
    case AmdGpu::NumberFormat::Uint:
    case AmdGpu::NumberFormat::Sint:
    case AmdGpu::NumberFormat::SnormNz:
    default:
        return false;
    }
}

IR::Type BufferDataType(const IR::Inst& inst, AmdGpu::NumberFormat num_format) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadBufferF32:
    case IR::Opcode::LoadBufferF32x2:
    case IR::Opcode::LoadBufferF32x3:
    case IR::Opcode::LoadBufferF32x4:
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::StoreBufferF32:
    case IR::Opcode::StoreBufferF32x2:
    case IR::Opcode::StoreBufferF32x3:
    case IR::Opcode::StoreBufferF32x4:
        return IR::Type::F32;
    case IR::Opcode::LoadBufferU32:
    case IR::Opcode::ReadConstBufferU32:
    case IR::Opcode::StoreBufferU32:
    case IR::Opcode::BufferAtomicIAdd32:
    case IR::Opcode::BufferAtomicSwap32:
        return IR::Type::U32;
    default:
        UNREACHABLE();
    }
}

bool IsImageAtomicInstruction(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::ImageAtomicIAdd32:
    case IR::Opcode::ImageAtomicSMin32:
    case IR::Opcode::ImageAtomicUMin32:
    case IR::Opcode::ImageAtomicSMax32:
    case IR::Opcode::ImageAtomicUMax32:
    case IR::Opcode::ImageAtomicInc32:
    case IR::Opcode::ImageAtomicDec32:
    case IR::Opcode::ImageAtomicAnd32:
    case IR::Opcode::ImageAtomicOr32:
    case IR::Opcode::ImageAtomicXor32:
    case IR::Opcode::ImageAtomicExchange32:
        return true;
    default:
        return false;
    }
}

bool IsImageStorageInstruction(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::ImageWrite:
    case IR::Opcode::ImageRead:
        return true;
    default:
        return IsImageAtomicInstruction(inst);
    }
}

bool IsImageInstruction(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::ImageSampleExplicitLod:
    case IR::Opcode::ImageSampleImplicitLod:
    case IR::Opcode::ImageSampleDrefExplicitLod:
    case IR::Opcode::ImageSampleDrefImplicitLod:
    case IR::Opcode::ImageFetch:
    case IR::Opcode::ImageGather:
    case IR::Opcode::ImageGatherDref:
    case IR::Opcode::ImageQueryDimensions:
    case IR::Opcode::ImageQueryLod:
    case IR::Opcode::ImageGradient:
        return true;
    default:
        return IsImageStorageInstruction(inst);
    }
}

u32 ImageOffsetArgumentPosition(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::ImageGather:
    case IR::Opcode::ImageGatherDref:
        return 2;
    case IR::Opcode::ImageSampleExplicitLod:
    case IR::Opcode::ImageSampleImplicitLod:
        return 3;
    case IR::Opcode::ImageSampleDrefExplicitLod:
    case IR::Opcode::ImageSampleDrefImplicitLod:
        return 4;
    default:
        UNREACHABLE();
    }
}

class Descriptors {
public:
    explicit Descriptors(Info& info_)
        : info{info_}, buffer_resources{info_.buffers},
          texture_buffer_resources{info_.texture_buffers}, image_resources{info_.images},
          sampler_resources{info_.samplers} {}

    u32 Add(const BufferResource& desc) {
        const u32 index{Add(buffer_resources, desc, [&desc](const auto& existing) {
            return desc.sgpr_base == existing.sgpr_base &&
                   desc.dword_offset == existing.dword_offset &&
                   desc.inline_cbuf == existing.inline_cbuf;
        })};
        auto& buffer = buffer_resources[index];
        buffer.used_types |= desc.used_types;
        buffer.is_written |= desc.is_written;
        return index;
    }

    u32 Add(const TextureBufferResource& desc) {
        const u32 index{Add(texture_buffer_resources, desc, [&desc](const auto& existing) {
            return desc.sgpr_base == existing.sgpr_base &&
                   desc.dword_offset == existing.dword_offset;
        })};
        auto& buffer = texture_buffer_resources[index];
        buffer.is_written |= desc.is_written;
        return index;
    }

    u32 Add(const ImageResource& desc) {
        const u32 index{Add(image_resources, desc, [&desc](const auto& existing) {
            return desc.sgpr_base == existing.sgpr_base &&
                   desc.dword_offset == existing.dword_offset && desc.type == existing.type &&
                   desc.is_storage == existing.is_storage;
        })};
        return index;
    }

    u32 Add(const SamplerResource& desc) {
        const u32 index{Add(sampler_resources, desc, [this, &desc](const auto& existing) {
            if (desc.sgpr_base == existing.sgpr_base &&
                desc.dword_offset == existing.dword_offset) {
                return true;
            }
            // Samplers with different bindings might still be the same.
            return existing.GetSharp(info) == desc.GetSharp(info);
        })};
        return index;
    }

private:
    template <typename Descriptors, typename Descriptor, typename Func>
    static u32 Add(Descriptors& descriptors, const Descriptor& desc, Func&& pred) {
        const auto it{std::ranges::find_if(descriptors, pred)};
        if (it != descriptors.end()) {
            return static_cast<u32>(std::distance(descriptors.begin(), it));
        }
        descriptors.push_back(desc);
        return static_cast<u32>(descriptors.size()) - 1;
    }

    const Info& info;
    BufferResourceList& buffer_resources;
    TextureBufferResourceList& texture_buffer_resources;
    ImageResourceList& image_resources;
    SamplerResourceList& sampler_resources;
};

} // Anonymous namespace

std::pair<const IR::Inst*, bool> TryDisableAnisoLod0(const IR::Inst* inst) {
    std::pair not_found{inst, false};

    // Assuming S# is in UD s[12:15] and T# is in s[4:11]
    // The next pattern:
    //  s_bfe_u32     s0, s7,  $0x0008000c
    //  s_and_b32     s1, s12, $0xfffff1ff
    //  s_cmp_eq_u32  s0, 0
    //  s_cselect_b32 s0, s1, s12
    // is used to disable anisotropy in the sampler if the sampled texture doesn't have mips

    if (inst->GetOpcode() != IR::Opcode::SelectU32) {
        return not_found;
    }

    // Select should be based on zero check
    const auto* prod0 = inst->Arg(0).InstRecursive();
    if (prod0->GetOpcode() != IR::Opcode::IEqual ||
        !(prod0->Arg(1).IsImmediate() && prod0->Arg(1).U32() == 0u)) {
        return not_found;
    }

    // The bits range is for lods (note that constants are changed after constant propagation pass)
    const auto* prod0_arg0 = prod0->Arg(0).InstRecursive();
    if (prod0_arg0->GetOpcode() != IR::Opcode::BitFieldUExtract ||
        !(prod0_arg0->Arg(1).IsIdentity() && prod0_arg0->Arg(1).U32() == 12) ||
        !(prod0_arg0->Arg(2).IsIdentity() && prod0_arg0->Arg(2).U32() == 8)) {
        return not_found;
    }

    // Make sure mask is masking out anisotropy
    const auto* prod1 = inst->Arg(1).InstRecursive();
    if (prod1->GetOpcode() != IR::Opcode::BitwiseAnd32 || prod1->Arg(1).U32() != 0xfffff1ff) {
        return not_found;
    }

    // We're working on the first dword of s#
    const auto* prod2 = inst->Arg(2).InstRecursive();
    if (prod2->GetOpcode() != IR::Opcode::GetUserData &&
        prod2->GetOpcode() != IR::Opcode::ReadConst) {
        return not_found;
    }

    return {prod2, true};
}

SharpLocation TrackSharp(const IR::Inst* inst) {
    // Search until we find a potential sharp source.
    const auto pred0 = [](const IR::Inst* inst) -> std::optional<const IR::Inst*> {
        if (inst->GetOpcode() == IR::Opcode::GetUserData ||
            inst->GetOpcode() == IR::Opcode::ReadConst) {
            return inst;
        }
        return std::nullopt;
    };
    const auto result = IR::BreadthFirstSearch(inst, pred0);
    ASSERT_MSG(result, "Unable to track sharp source");
    inst = result.value();
    // If its from user data not much else to do.
    if (inst->GetOpcode() == IR::Opcode::GetUserData) {
        return SharpLocation{
            .sgpr_base = u32(IR::ScalarReg::Max),
            .dword_offset = u32(inst->Arg(0).ScalarReg()),
        };
    }
    ASSERT_MSG(inst->GetOpcode() == IR::Opcode::ReadConst, "Sharp load not from constant memory");

    // Retrieve offset from base.
    const u32 dword_offset = inst->Arg(1).U32();
    const IR::Inst* spgpr_base = inst->Arg(0).InstRecursive();

    // Retrieve SGPR pair that holds sbase
    const auto pred1 = [](const IR::Inst* inst) -> std::optional<IR::ScalarReg> {
        ASSERT(inst->GetOpcode() != IR::Opcode::ReadConst);
        if (inst->GetOpcode() == IR::Opcode::GetUserData) {
            return inst->Arg(0).ScalarReg();
        }
        return std::nullopt;
    };
    const auto base0 = IR::BreadthFirstSearch(spgpr_base->Arg(0), pred1);
    const auto base1 = IR::BreadthFirstSearch(spgpr_base->Arg(1), pred1);
    ASSERT_MSG(base0 && base1, "Nested resource loads not supported");

    // Return retrieved location.
    return SharpLocation{
        .sgpr_base = u32(base0.value()),
        .dword_offset = dword_offset,
    };
}

s32 TryHandleInlineCbuf(IR::Inst& inst, Info& info, Descriptors& descriptors,
                        AmdGpu::Buffer& cbuf) {

    // Assuming V# is in UD s[32:35]
    // The next pattern:
    // s_getpc_b64     s[32:33]
    // s_add_u32       s32, <const>, s32
    // s_addc_u32      s33, 0, s33
    // s_mov_b32       s35, <const>
    // s_movk_i32      s34, <const>
    // buffer_load_format_xyz v[8:10], v1, s[32:35], 0 ...
    // is used to define an inline constant buffer

    IR::Inst* handle = inst.Arg(0).InstRecursive();
    if (!handle->AreAllArgsImmediates()) {
        return -1;
    }
    // We have found this pattern. Build the sharp.
    std::array<u64, 2> buffer;
    buffer[0] = info.pgm_base + (handle->Arg(0).U32() | u64(handle->Arg(1).U32()) << 32);
    buffer[1] = handle->Arg(2).U32() | u64(handle->Arg(3).U32()) << 32;
    cbuf = std::bit_cast<AmdGpu::Buffer>(buffer);
    // Assign a binding to this sharp.
    return descriptors.Add(BufferResource{
        .sgpr_base = std::numeric_limits<u32>::max(),
        .dword_offset = 0,
        .used_types = BufferDataType(inst, cbuf.GetNumberFmt()),
        .inline_cbuf = cbuf,
    });
}

void PatchBufferInstruction(IR::Block& block, IR::Inst& inst, Info& info,
                            Descriptors& descriptors) {
    s32 binding{};
    AmdGpu::Buffer buffer;
    if (binding = TryHandleInlineCbuf(inst, info, descriptors, buffer); binding == -1) {
        IR::Inst* handle = inst.Arg(0).InstRecursive();
        IR::Inst* producer = handle->Arg(0).InstRecursive();
        const auto sharp = TrackSharp(producer);
        buffer = info.ReadUd<AmdGpu::Buffer>(sharp.sgpr_base, sharp.dword_offset);
        binding = descriptors.Add(BufferResource{
            .sgpr_base = sharp.sgpr_base,
            .dword_offset = sharp.dword_offset,
            .used_types = BufferDataType(inst, buffer.GetNumberFmt()),
            .is_written = IsBufferStore(inst),
        });
    }

    // Update buffer descriptor format.
    const auto inst_info = inst.Flags<IR::BufferInstInfo>();

    // Replace handle with binding index in buffer resource list.
    IR::IREmitter ir{block, IR::Block::InstructionList::s_iterator_to(inst)};
    inst.SetArg(0, ir.Imm32(binding));
    ASSERT(!buffer.swizzle_enable && !buffer.add_tid_enable);

    // Address of constant buffer reads can be calculated at IR emittion time.
    if (inst.GetOpcode() == IR::Opcode::ReadConstBuffer ||
        inst.GetOpcode() == IR::Opcode::ReadConstBufferU32) {
        return;
    }

    // Compute address of the buffer using the stride.
    IR::U32 address = ir.Imm32(inst_info.inst_offset.Value());
    if (inst_info.index_enable) {
        const IR::U32 index = inst_info.offset_enable ? IR::U32{ir.CompositeExtract(inst.Arg(1), 0)}
                                                      : IR::U32{inst.Arg(1)};
        address = ir.IAdd(address, ir.IMul(index, ir.Imm32(buffer.GetStride())));
    }
    if (inst_info.offset_enable) {
        const IR::U32 offset = inst_info.index_enable ? IR::U32{ir.CompositeExtract(inst.Arg(1), 1)}
                                                      : IR::U32{inst.Arg(1)};
        address = ir.IAdd(address, offset);
    }
    inst.SetArg(1, address);
}

void PatchTextureBufferInstruction(IR::Block& block, IR::Inst& inst, Info& info,
                                   Descriptors& descriptors) {
    const IR::Inst* handle = inst.Arg(0).InstRecursive();
    const IR::Inst* producer = handle->Arg(0).InstRecursive();
    const auto sharp = TrackSharp(producer);
    const auto buffer = info.ReadUd<AmdGpu::Buffer>(sharp.sgpr_base, sharp.dword_offset);
    const s32 binding = descriptors.Add(TextureBufferResource{
        .sgpr_base = sharp.sgpr_base,
        .dword_offset = sharp.dword_offset,
        .nfmt = buffer.GetNumberFmt(),
        .is_written = inst.GetOpcode() == IR::Opcode::StoreBufferFormatF32,
    });

    // Replace handle with binding index in texture buffer resource list.
    IR::IREmitter ir{block, IR::Block::InstructionList::s_iterator_to(inst)};
    inst.SetArg(0, ir.Imm32(binding));
    ASSERT(!buffer.swizzle_enable && !buffer.add_tid_enable);
}

IR::Value PatchCubeCoord(IR::IREmitter& ir, const IR::Value& s, const IR::Value& t,
                         const IR::Value& z, bool is_storage) {
    // When cubemap is written with imageStore it is treated like 2DArray.
    if (is_storage) {
        return ir.CompositeConstruct(s, t, z);
    }
    // We need to fix x and y coordinate,
    // because the s and t coordinate will be scaled and plus 1.5 by v_madak_f32.
    // We already force the scale value to be 1.0 when handling v_cubema_f32,
    // here we subtract 1.5 to recover the original value.
    const IR::Value x = ir.FPSub(IR::F32{s}, ir.Imm32(1.5f));
    const IR::Value y = ir.FPSub(IR::F32{t}, ir.Imm32(1.5f));
    return ir.CompositeConstruct(x, y, z);
}

void PatchImageInstruction(IR::Block& block, IR::Inst& inst, Info& info, Descriptors& descriptors) {
    const auto pred = [](const IR::Inst* inst) -> std::optional<const IR::Inst*> {
        const auto opcode = inst->GetOpcode();
        if (opcode == IR::Opcode::CompositeConstructU32x2 || // IMAGE_SAMPLE (image+sampler)
            opcode == IR::Opcode::ReadConst ||               // IMAGE_LOAD (image only)
            opcode == IR::Opcode::GetUserData) {
            return inst;
        }
        return std::nullopt;
    };
    const auto result = IR::BreadthFirstSearch(&inst, pred);
    ASSERT_MSG(result, "Unable to find image sharp source");
    const IR::Inst* producer = result.value();
    const bool has_sampler = producer->GetOpcode() == IR::Opcode::CompositeConstructU32x2;
    const auto tsharp_handle = has_sampler ? producer->Arg(0).InstRecursive() : producer;

    // Read image sharp.
    const auto tsharp = TrackSharp(tsharp_handle);
    const auto image = info.ReadUd<AmdGpu::Image>(tsharp.sgpr_base, tsharp.dword_offset);
    const auto inst_info = inst.Flags<IR::TextureInstInfo>();
    if (!image.Valid()) {
        LOG_ERROR(Render_Vulkan, "Shader compiled with unbound image!");
        IR::IREmitter ir{block, IR::Block::InstructionList::s_iterator_to(inst)};
        inst.ReplaceUsesWith(
            ir.CompositeConstruct(ir.Imm32(0.f), ir.Imm32(0.f), ir.Imm32(0.f), ir.Imm32(0.f)));
        return;
    }
    ASSERT(image.GetType() != AmdGpu::ImageType::Invalid);
    const bool is_storage = IsImageStorageInstruction(inst);
    u32 image_binding = descriptors.Add(ImageResource{
        .sgpr_base = tsharp.sgpr_base,
        .dword_offset = tsharp.dword_offset,
        .type = image.GetType(),
        .nfmt = static_cast<AmdGpu::NumberFormat>(image.GetNumberFmt()),
        .is_storage = is_storage,
        .is_depth = bool(inst_info.is_depth),
        .is_atomic = IsImageAtomicInstruction(inst),
    });

    // Read sampler sharp. This doesn't exist for IMAGE_LOAD/IMAGE_STORE instructions
    const u32 sampler_binding = [&] {
        if (!has_sampler) {
            return 0U;
        }
        const IR::Value& handle = producer->Arg(1);
        // Inline sampler resource.
        if (handle.IsImmediate()) {
            LOG_WARNING(Render_Vulkan, "Inline sampler detected");
            return descriptors.Add(SamplerResource{
                .sgpr_base = std::numeric_limits<u32>::max(),
                .dword_offset = 0,
                .inline_sampler = AmdGpu::Sampler{.raw0 = handle.U32()},
            });
        }
        // Normal sampler resource.
        const auto ssharp_handle = handle.InstRecursive();
        const auto& [ssharp_ud, disable_aniso] = TryDisableAnisoLod0(ssharp_handle);
        const auto ssharp = TrackSharp(ssharp_ud);
        return descriptors.Add(SamplerResource{
            .sgpr_base = ssharp.sgpr_base,
            .dword_offset = ssharp.dword_offset,
            .associated_image = image_binding,
            .disable_aniso = disable_aniso,
        });
    }();
    image_binding |= (sampler_binding << 16);

    // Patch image handle
    IR::IREmitter ir{block, IR::Block::InstructionList::s_iterator_to(inst)};
    inst.SetArg(0, ir.Imm32(image_binding));

    // No need to patch coordinates if we are just querying.
    if (inst.GetOpcode() == IR::Opcode::ImageQueryDimensions) {
        return;
    }

    // Now that we know the image type, adjust texture coordinate vector.
    IR::Inst* body = inst.Arg(1).InstRecursive();
    const auto [coords, arg] = [&] -> std::pair<IR::Value, IR::Value> {
        switch (image.GetType()) {
        case AmdGpu::ImageType::Color1D: // x
            return {body->Arg(0), body->Arg(1)};
        case AmdGpu::ImageType::Color1DArray: // x, slice
            [[fallthrough]];
        case AmdGpu::ImageType::Color2D: // x, y
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1)), body->Arg(2)};
        case AmdGpu::ImageType::Color2DArray: // x, y, slice
            [[fallthrough]];
        case AmdGpu::ImageType::Color2DMsaa: // x, y, frag
            [[fallthrough]];
        case AmdGpu::ImageType::Color3D: // x, y, z
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1), body->Arg(2)), body->Arg(3)};
        case AmdGpu::ImageType::Cube: // x, y, face
            return {PatchCubeCoord(ir, body->Arg(0), body->Arg(1), body->Arg(2), is_storage),
                    body->Arg(3)};
        default:
            UNREACHABLE_MSG("Unknown image type {}", image.GetType());
        }
    }();
    inst.SetArg(1, coords);

    if (inst_info.has_offset) {
        // The offsets are six-bit signed integers: X=[5:0], Y=[13:8], and Z=[21:16].
        const u32 arg_pos = ImageOffsetArgumentPosition(inst);
        const IR::Value arg = inst.Arg(arg_pos);
        ASSERT_MSG(arg.Type() == IR::Type::U32, "Unexpected offset type");

        const auto read = [&](u32 offset) -> IR::U32 {
            if (arg.IsImmediate()) {
                const u16 comp = (arg.U32() >> offset) & 0x3F;
                return ir.Imm32(s32(comp << 26) >> 26);
            }
            return ir.BitFieldExtract(IR::U32{arg}, ir.Imm32(offset), ir.Imm32(6), true);
        };

        switch (image.GetType()) {
        case AmdGpu::ImageType::Color1D:
        case AmdGpu::ImageType::Color1DArray:
            inst.SetArg(arg_pos, read(0));
            break;
        case AmdGpu::ImageType::Color2D:
        case AmdGpu::ImageType::Color2DArray:
            inst.SetArg(arg_pos, ir.CompositeConstruct(read(0), read(8)));
            break;
        case AmdGpu::ImageType::Color3D:
            inst.SetArg(arg_pos, ir.CompositeConstruct(read(0), read(8), read(16)));
            break;
        default:
            UNREACHABLE();
        }
    }
    if (inst_info.has_derivatives) {
        ASSERT_MSG(image.GetType() == AmdGpu::ImageType::Color2D,
                   "User derivatives only supported for 2D images");
    }
    if (inst_info.has_lod_clamp) {
        const u32 arg_pos = [&]() -> u32 {
            switch (inst.GetOpcode()) {
            case IR::Opcode::ImageSampleImplicitLod:
                return 2;
            case IR::Opcode::ImageSampleDrefImplicitLod:
                return 3;
            default:
                break;
            }
            return inst_info.is_depth ? 5 : 4;
        }();
        inst.SetArg(arg_pos, arg);
    }
    if (inst_info.explicit_lod) {
        ASSERT(inst.GetOpcode() == IR::Opcode::ImageFetch ||
               inst.GetOpcode() == IR::Opcode::ImageSampleExplicitLod ||
               inst.GetOpcode() == IR::Opcode::ImageSampleDrefExplicitLod);
        const u32 pos = inst.GetOpcode() == IR::Opcode::ImageSampleExplicitLod ? 2 : 3;
        const IR::Value value = inst_info.force_level0 ? ir.Imm32(0.f) : arg;
        inst.SetArg(pos, value);
    }
}

void ResourceTrackingPass(IR::Program& program) {
    // Iterate resource instructions and patch them after finding the sharp.
    auto& info = program.info;
    Descriptors descriptors{info};
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (IsBufferInstruction(inst)) {
                PatchBufferInstruction(*block, inst, info, descriptors);
                continue;
            }
            if (IsTextureBufferInstruction(inst)) {
                PatchTextureBufferInstruction(*block, inst, info, descriptors);
                continue;
            }
            if (IsImageInstruction(inst)) {
                PatchImageInstruction(*block, inst, info, descriptors);
            }
        }
    }
}

} // namespace Shader::Optimization
