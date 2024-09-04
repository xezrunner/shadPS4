// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/alignment.h"
#include "common/scope_exit.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {

static constexpr size_t StagingBufferSize = 512_MB;
static constexpr size_t UboStreamBufferSize = 64_MB;

BufferCache::BufferCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                         const AmdGpu::Liverpool* liverpool_, PageManager& tracker_)
    : instance{instance_}, scheduler{scheduler_}, liverpool{liverpool_}, tracker{tracker_},
      staging_buffer{instance, scheduler, MemoryUsage::Upload, StagingBufferSize},
      stream_buffer{instance, scheduler, MemoryUsage::Stream, UboStreamBufferSize},
      memory_tracker{&tracker} {
    // Ensure the first slot is used for the null buffer
    void(slot_buffers.insert(instance, MemoryUsage::DeviceLocal, 0, ReadFlags, 1));
}

BufferCache::~BufferCache() = default;

void BufferCache::InvalidateMemory(VAddr device_addr, u64 size) {
    std::scoped_lock lk{mutex};
    const bool is_tracked = IsRegionRegistered(device_addr, size);
    if (!is_tracked) {
        return;
    }
    // Mark the page as CPU modified to stop tracking writes.
    SCOPE_EXIT {
        memory_tracker.MarkRegionAsCpuModified(device_addr, size);
    };
    if (!memory_tracker.IsRegionGpuModified(device_addr, size)) {
        // Page has not been modified by the GPU, nothing to do.
        return;
    }
}

void BufferCache::DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size) {
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    memory_tracker.ForEachDownloadRange<true>(
        device_addr, size, [&](u64 device_addr_out, u64 range_size) {
            const VAddr buffer_addr = buffer.CpuAddr();
            const auto add_download = [&](VAddr start, VAddr end, u64) {
                const u64 new_offset = start - buffer_addr;
                const u64 new_size = end - start;
                copies.push_back(vk::BufferCopy{
                    .srcOffset = new_offset,
                    .dstOffset = total_size_bytes,
                    .size = new_size,
                });
                // Align up to avoid cache conflicts
                constexpr u64 align = 64ULL;
                constexpr u64 mask = ~(align - 1ULL);
                total_size_bytes += (new_size + align - 1) & mask;
                largest_copy = std::max(largest_copy, new_size);
            };
        });
    if (total_size_bytes == 0) {
        return;
    }
    const auto [staging, offset] = staging_buffer.Map(total_size_bytes);
    for (auto& copy : copies) {
        // Modify copies to have the staging offset in mind
        copy.dstOffset += offset;
    }
    staging_buffer.Commit();
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(buffer.buffer, staging_buffer.Handle(), copies);
    scheduler.Finish();
    for (const auto& copy : copies) {
        const VAddr copy_device_addr = buffer.CpuAddr() + copy.srcOffset;
        const u64 dst_offset = copy.dstOffset - offset;
        std::memcpy(std::bit_cast<u8*>(copy_device_addr), staging + dst_offset, copy.size);
    }
}

bool BufferCache::BindVertexBuffers(const Shader::Info& vs_info) {
    boost::container::small_vector<vk::VertexInputAttributeDescription2EXT, 16> attributes;
    boost::container::small_vector<vk::VertexInputBindingDescription2EXT, 16> bindings;
    SCOPE_EXIT {
        if (instance.IsVertexInputDynamicState()) {
            const auto cmdbuf = scheduler.CommandBuffer();
            cmdbuf.setVertexInputEXT(bindings, attributes);
        }
    };

    if (vs_info.vs_inputs.empty()) {
        return false;
    }

    std::array<vk::Buffer, NUM_VERTEX_BUFFERS> host_buffers;
    std::array<vk::DeviceSize, NUM_VERTEX_BUFFERS> host_offsets;
    boost::container::static_vector<AmdGpu::Buffer, NUM_VERTEX_BUFFERS> guest_buffers;

    struct BufferRange {
        VAddr base_address;
        VAddr end_address;
        vk::Buffer vk_buffer;
        u64 offset;

        size_t GetSize() const {
            return end_address - base_address;
        }
    };

    // Calculate buffers memory overlaps
    bool has_step_rate = false;
    boost::container::static_vector<BufferRange, NUM_VERTEX_BUFFERS> ranges{};
    for (const auto& input : vs_info.vs_inputs) {
        if (input.instance_step_rate == Shader::Info::VsInput::InstanceIdType::OverStepRate0 ||
            input.instance_step_rate == Shader::Info::VsInput::InstanceIdType::OverStepRate1) {
            has_step_rate = true;
            continue;
        }

        const auto& buffer = vs_info.ReadUd<AmdGpu::Buffer>(input.sgpr_base, input.dword_offset);
        if (buffer.GetSize() == 0) {
            continue;
        }
        guest_buffers.emplace_back(buffer);
        ranges.emplace_back(buffer.base_address, buffer.base_address + buffer.GetSize());
        attributes.push_back({
            .location = input.binding,
            .binding = input.binding,
            .format =
                Vulkan::LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt()),
            .offset = 0,
        });
        bindings.push_back({
            .binding = input.binding,
            .stride = buffer.GetStride(),
            .inputRate = input.instance_step_rate == Shader::Info::VsInput::None
                             ? vk::VertexInputRate::eVertex
                             : vk::VertexInputRate::eInstance,
            .divisor = 1,
        });
    }

    std::ranges::sort(ranges, [](const BufferRange& lhv, const BufferRange& rhv) {
        return lhv.base_address < rhv.base_address;
    });

    boost::container::static_vector<BufferRange, NUM_VERTEX_BUFFERS> ranges_merged{ranges[0]};
    for (auto range : ranges) {
        auto& prev_range = ranges_merged.back();
        if (prev_range.end_address < range.base_address) {
            ranges_merged.emplace_back(range);
        } else {
            prev_range.end_address = std::max(prev_range.end_address, range.end_address);
        }
    }

    // Map buffers
    for (auto& range : ranges_merged) {
        const auto [buffer, offset] = ObtainBuffer(range.base_address, range.GetSize(), false);
        range.vk_buffer = buffer->buffer;
        range.offset = offset;
    }

    // Bind vertex buffers
    const size_t num_buffers = guest_buffers.size();
    for (u32 i = 0; i < num_buffers; ++i) {
        const auto& buffer = guest_buffers[i];
        const auto host_buffer = std::ranges::find_if(ranges_merged, [&](const BufferRange& range) {
            return (buffer.base_address >= range.base_address &&
                    buffer.base_address < range.end_address);
        });
        ASSERT(host_buffer != ranges_merged.cend());

        host_buffers[i] = host_buffer->vk_buffer;
        host_offsets[i] = host_buffer->offset + buffer.base_address - host_buffer->base_address;
    }

    if (num_buffers > 0) {
        const auto cmdbuf = scheduler.CommandBuffer();
        cmdbuf.bindVertexBuffers(0, num_buffers, host_buffers.data(), host_offsets.data());
    }

    return has_step_rate;
}

u32 BufferCache::BindIndexBuffer(bool& is_indexed, u32 index_offset) {
    // Emulate QuadList primitive type with CPU made index buffer.
    const auto& regs = liverpool->regs;
    if (regs.primitive_type == AmdGpu::Liverpool::PrimitiveType::QuadList) {
        is_indexed = true;

        // Emit indices.
        const u32 index_size = 3 * regs.num_indices;
        const auto [data, offset] = stream_buffer.Map(index_size);
        Vulkan::LiverpoolToVK::EmitQuadToTriangleListIndices(data, regs.num_indices);
        stream_buffer.Commit();

        // Bind index buffer.
        const auto cmdbuf = scheduler.CommandBuffer();
        cmdbuf.bindIndexBuffer(stream_buffer.Handle(), offset, vk::IndexType::eUint16);
        return index_size / sizeof(u16);
    }
    if (!is_indexed) {
        return regs.num_indices;
    }

    // Figure out index type and size.
    const bool is_index16 =
        regs.index_buffer_type.index_type == AmdGpu::Liverpool::IndexType::Index16;
    const vk::IndexType index_type = is_index16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const u32 index_size = is_index16 ? sizeof(u16) : sizeof(u32);
    VAddr index_address = regs.index_base_address.Address<VAddr>();
    index_address += index_offset * index_size;

    // Bind index buffer.
    const u32 index_buffer_size = regs.num_indices * index_size;
    const auto [vk_buffer, offset] = ObtainBuffer(index_address, index_buffer_size, false);
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.bindIndexBuffer(vk_buffer->Handle(), offset, index_type);
    return regs.num_indices;
}

std::pair<Buffer*, u32> BufferCache::ObtainBuffer(VAddr device_addr, u32 size, bool is_written,
                                                  bool is_texel_buffer) {
    std::scoped_lock lk{mutex};
    static constexpr u64 StreamThreshold = CACHING_PAGESIZE;
    const bool is_gpu_dirty = memory_tracker.IsRegionGpuModified(device_addr, size);
    if (!is_written && !is_texel_buffer && size <= StreamThreshold && !is_gpu_dirty) {
        // For small uniform buffers that have not been modified by gpu
        // use device local stream buffer to reduce renderpass breaks.
        const u64 offset = stream_buffer.Copy(device_addr, size, instance.UniformMinAlignment());
        return {&stream_buffer, offset};
    }

    const BufferId buffer_id = FindBuffer(device_addr, size);
    Buffer& buffer = slot_buffers[buffer_id];
    SynchronizeBuffer(buffer, device_addr, size);
    if (is_written) {
        memory_tracker.MarkRegionAsGpuModified(device_addr, size);
    }
    return {&buffer, buffer.Offset(device_addr)};
}

std::pair<const Buffer*, u32> BufferCache::ObtainTempBuffer(VAddr gpu_addr, u32 size) {
    const u64 page = gpu_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = page_table[page];
    if (buffer_id) {
        const Buffer& buffer = slot_buffers[buffer_id];
        if (buffer.IsInBounds(gpu_addr, size)) {
            return {&buffer, buffer.Offset(gpu_addr)};
        }
    }
    const u32 offset = staging_buffer.Copy(gpu_addr, size, 16);
    return {&staging_buffer, offset};
}

bool BufferCache::IsRegionRegistered(VAddr addr, size_t size) {
    const VAddr end_addr = addr + size;
    const u64 page_end = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    for (u64 page = addr >> CACHING_PAGEBITS; page < page_end;) {
        const BufferId buffer_id = page_table[page];
        if (!buffer_id) {
            ++page;
            continue;
        }
        Buffer& buffer = slot_buffers[buffer_id];
        const VAddr buf_start_addr = buffer.CpuAddr();
        const VAddr buf_end_addr = buf_start_addr + buffer.SizeBytes();
        if (buf_start_addr < end_addr && addr < buf_end_addr) {
            return true;
        }
        page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    }
    return false;
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker.IsRegionCpuModified(addr, size);
}

bool BufferCache::IsRegionGpuModified(VAddr addr, size_t size) {
    return memory_tracker.IsRegionGpuModified(addr, size);
}

BufferId BufferCache::FindBuffer(VAddr device_addr, u32 size) {
    if (device_addr == 0) {
        return NULL_BUFFER_ID;
    }
    const u64 page = device_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = page_table[page];
    if (!buffer_id) {
        return CreateBuffer(device_addr, size);
    }
    const Buffer& buffer = slot_buffers[buffer_id];
    if (buffer.IsInBounds(device_addr, size)) {
        return buffer_id;
    }
    return CreateBuffer(device_addr, size);
}

BufferCache::OverlapResult BufferCache::ResolveOverlaps(VAddr device_addr, u32 wanted_size) {
    static constexpr int STREAM_LEAP_THRESHOLD = 16;
    boost::container::small_vector<BufferId, 16> overlap_ids;
    VAddr begin = device_addr;
    VAddr end = device_addr + wanted_size;
    int stream_score = 0;
    bool has_stream_leap = false;
    const auto expand_begin = [&](VAddr add_value) {
        static constexpr VAddr min_page = CACHING_PAGESIZE + DEVICE_PAGESIZE;
        if (add_value > begin - min_page) {
            begin = min_page;
            device_addr = DEVICE_PAGESIZE;
            return;
        }
        begin -= add_value;
        device_addr = begin - CACHING_PAGESIZE;
    };
    const auto expand_end = [&](VAddr add_value) {
        static constexpr VAddr max_page = 1ULL << MemoryTracker::MAX_CPU_PAGE_BITS;
        if (add_value > max_page - end) {
            end = max_page;
            return;
        }
        end += add_value;
    };
    if (begin == 0) {
        return OverlapResult{
            .ids = std::move(overlap_ids),
            .begin = begin,
            .end = end,
            .has_stream_leap = has_stream_leap,
        };
    }
    for (; device_addr >> CACHING_PAGEBITS < Common::DivCeil(end, CACHING_PAGESIZE);
         device_addr += CACHING_PAGESIZE) {
        const BufferId overlap_id = page_table[device_addr >> CACHING_PAGEBITS];
        if (!overlap_id) {
            continue;
        }
        Buffer& overlap = slot_buffers[overlap_id];
        if (overlap.is_picked) {
            continue;
        }
        overlap_ids.push_back(overlap_id);
        overlap.is_picked = true;
        const VAddr overlap_device_addr = overlap.CpuAddr();
        const bool expands_left = overlap_device_addr < begin;
        if (expands_left) {
            begin = overlap_device_addr;
        }
        const VAddr overlap_end = overlap_device_addr + overlap.SizeBytes();
        const bool expands_right = overlap_end > end;
        if (overlap_end > end) {
            end = overlap_end;
        }
        stream_score += overlap.StreamScore();
        if (stream_score > STREAM_LEAP_THRESHOLD && !has_stream_leap) {
            // When this memory region has been joined a bunch of times, we assume it's being used
            // as a stream buffer. Increase the size to skip constantly recreating buffers.
            has_stream_leap = true;
            if (expands_right) {
                expand_begin(CACHING_PAGESIZE * 128);
            }
            if (expands_left) {
                expand_end(CACHING_PAGESIZE * 128);
            }
        }
    }
    return OverlapResult{
        .ids = std::move(overlap_ids),
        .begin = begin,
        .end = end,
        .has_stream_leap = has_stream_leap,
    };
}

void BufferCache::JoinOverlap(BufferId new_buffer_id, BufferId overlap_id,
                              bool accumulate_stream_score) {
    Buffer& new_buffer = slot_buffers[new_buffer_id];
    Buffer& overlap = slot_buffers[overlap_id];
    if (accumulate_stream_score) {
        new_buffer.IncreaseStreamScore(overlap.StreamScore() + 1);
    }
    const size_t dst_base_offset = overlap.CpuAddr() - new_buffer.CpuAddr();
    const vk::BufferCopy copy = {
        .srcOffset = 0,
        .dstOffset = dst_base_offset,
        .size = overlap.SizeBytes(),
    };
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    static constexpr vk::MemoryBarrier READ_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite,
    };
    static constexpr vk::MemoryBarrier WRITE_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           READ_BARRIER, {}, {});
    cmdbuf.copyBuffer(overlap.buffer, new_buffer.buffer, copy);
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eAllCommands,
                           vk::DependencyFlagBits::eByRegion, WRITE_BARRIER, {}, {});
    DeleteBuffer(overlap_id, true);
}

BufferId BufferCache::CreateBuffer(VAddr device_addr, u32 wanted_size) {
    const VAddr device_addr_end = Common::AlignUp(device_addr + wanted_size, CACHING_PAGESIZE);
    device_addr = Common::AlignDown(device_addr, CACHING_PAGESIZE);
    wanted_size = static_cast<u32>(device_addr_end - device_addr);
    const OverlapResult overlap = ResolveOverlaps(device_addr, wanted_size);
    const u32 size = static_cast<u32>(overlap.end - overlap.begin);
    const BufferId new_buffer_id =
        slot_buffers.insert(instance, MemoryUsage::DeviceLocal, overlap.begin, AllFlags, size);
    auto& new_buffer = slot_buffers[new_buffer_id];
    const size_t size_bytes = new_buffer.SizeBytes();
    const auto cmdbuf = scheduler.CommandBuffer();
    scheduler.EndRendering();
    cmdbuf.fillBuffer(new_buffer.buffer, 0, size_bytes, 0);
    for (const BufferId overlap_id : overlap.ids) {
        JoinOverlap(new_buffer_id, overlap_id, !overlap.has_stream_leap);
    }
    Register(new_buffer_id);
    return new_buffer_id;
}

void BufferCache::Register(BufferId buffer_id) {
    ChangeRegister<true>(buffer_id);
}

void BufferCache::Unregister(BufferId buffer_id) {
    ChangeRegister<false>(buffer_id);
}

template <bool insert>
void BufferCache::ChangeRegister(BufferId buffer_id) {
    Buffer& buffer = slot_buffers[buffer_id];
    const auto size = buffer.SizeBytes();
    const VAddr device_addr_begin = buffer.CpuAddr();
    const VAddr device_addr_end = device_addr_begin + size;
    const u64 page_begin = device_addr_begin / CACHING_PAGESIZE;
    const u64 page_end = Common::DivCeil(device_addr_end, CACHING_PAGESIZE);
    for (u64 page = page_begin; page != page_end; ++page) {
        if constexpr (insert) {
            page_table[page] = buffer_id;
        } else {
            page_table[page] = BufferId{};
        }
    }
}

bool BufferCache::SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size) {
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    VAddr buffer_start = buffer.CpuAddr();
    const auto add_copy = [&](VAddr device_addr_out, u64 range_size) {
        copies.push_back(vk::BufferCopy{
            .srcOffset = total_size_bytes,
            .dstOffset = device_addr_out - buffer_start,
            .size = range_size,
        });
        total_size_bytes += range_size;
        largest_copy = std::max(largest_copy, range_size);
    };
    memory_tracker.ForEachUploadRange(device_addr, size, [&](u64 device_addr_out, u64 range_size) {
        add_copy(device_addr_out, range_size);
        // Prevent uploading to gpu modified regions.
        // gpu_modified_ranges.ForEachNotInRange(device_addr_out, range_size, add_copy);
    });
    if (total_size_bytes == 0) {
        return true;
    }
    vk::Buffer src_buffer = staging_buffer.Handle();
    if (total_size_bytes < StagingBufferSize) {
        const auto [staging, offset] = staging_buffer.Map(total_size_bytes);
        for (auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            std::memcpy(src_pointer, std::bit_cast<const u8*>(device_addr), copy.size);
            // Apply the staging offset
            copy.srcOffset += offset;
        }
        staging_buffer.Commit();
    } else {
        // For large one time transfers use a temporary host buffer.
        // RenderDoc can lag quite a bit if the stream buffer is too large.
        Buffer temp_buffer{instance, MemoryUsage::Upload, 0, vk::BufferUsageFlagBits::eTransferSrc,
                           total_size_bytes};
        src_buffer = temp_buffer.Handle();
        u8* const staging = temp_buffer.mapped_data.data();
        for (auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            std::memcpy(src_pointer, std::bit_cast<const u8*>(device_addr), copy.size);
        }
        scheduler.DeferOperation([buffer = std::move(temp_buffer)]() mutable {});
    }
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    static constexpr vk::MemoryBarrier READ_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite,
    };
    static constexpr vk::MemoryBarrier WRITE_BARRIER{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           READ_BARRIER, {}, {});
    cmdbuf.copyBuffer(src_buffer, buffer.buffer, copies);
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eAllCommands,
                           vk::DependencyFlagBits::eByRegion, WRITE_BARRIER, {}, {});
    return false;
}

void BufferCache::DeleteBuffer(BufferId buffer_id, bool do_not_mark) {
    // Mark the whole buffer as CPU written to stop tracking CPU writes
    if (!do_not_mark) {
        Buffer& buffer = slot_buffers[buffer_id];
        memory_tracker.MarkRegionAsCpuModified(buffer.CpuAddr(), buffer.SizeBytes());
    }
    Unregister(buffer_id);
    scheduler.DeferOperation([this, buffer_id] { slot_buffers.erase(buffer_id); });
}

} // namespace VideoCore
