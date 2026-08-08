// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/alignment.h"
#include "common/debug.h"
#include "common/scope_exit.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"

namespace VideoCore {

static constexpr size_t DataShareBufferSize = 64_KB;
static constexpr size_t StagingBufferSize = 512_MB;
static constexpr size_t DownloadBufferSize = 32_MB;
// Host-visible ring; must hold MaxWritebackBatches full budgets so a batch cannot be overwritten
// before it has been handed over.
static constexpr size_t WritebackBufferSize = 96_MB;
static constexpr size_t UboStreamBufferSize = 64_MB;
static constexpr size_t DeviceBufferSize = 128_MB;

// Writes up to this size are the small control blocks readback consumers poll (counters, emitter
// headers); with readbacks disabled they are read-guarded and served at read time. Writes past it
// are bulk GPU output (streamout, particle state) that only the GPU consumes again; it is not
// carried back at all.
static constexpr u64 WritebackWriteLimit = 1_MB;
// Cap on how much one boundary may stage; control-block batches stay far below it.
static constexpr u64 WritebackBudget = 40_MB;
static constexpr size_t MaxWritebackBatches = 2;

BufferCache::BufferCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
                         AmdGpu::Liverpool* liverpool_, TextureCache& texture_cache_,
                         PageManager& tracker)
    : instance{instance_}, scheduler{scheduler_}, liverpool{liverpool_},
      memory{Core::Memory::Instance()}, texture_cache{texture_cache_},
      fault_manager{instance, scheduler, *this, CACHING_PAGEBITS, CACHING_NUMPAGES},
      staging_buffer{instance, scheduler, MemoryUsage::Upload, StagingBufferSize},
      stream_buffer{instance, scheduler, MemoryUsage::Stream, UboStreamBufferSize},
      download_buffer{instance, scheduler, MemoryUsage::Download, DownloadBufferSize},
      writeback_buffer{instance, scheduler, MemoryUsage::Download, WritebackBufferSize},
      device_buffer{instance, scheduler, MemoryUsage::DeviceLocal, DeviceBufferSize},
      gds_buffer{instance, scheduler, MemoryUsage::Stream, 0, AllFlags, DataShareBufferSize},
      bda_pagetable_buffer{instance, scheduler, MemoryUsage::DeviceLocal,
                           0,        AllFlags,  BDA_PAGETABLE_SIZE} {
    Vulkan::SetObjectName(instance.GetDevice(), gds_buffer.Handle(), "GDS Buffer");
    Vulkan::SetObjectName(instance.GetDevice(), bda_pagetable_buffer.Handle(),
                          "BDA Page Table Buffer");

    memory_tracker = std::make_unique<MemoryTracker>(tracker);
    async_writeback = EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Precise;

    std::memset(gds_buffer.mapped_data.data(), 0, DataShareBufferSize);

    // Set up garbage collection parameters
    if (!instance.CanReportMemoryUsage()) {
        trigger_gc_memory = DEFAULT_TRIGGER_GC_MEMORY;
        critical_gc_memory = DEFAULT_CRITICAL_GC_MEMORY;
        return;
    }

    const s64 device_local_memory = static_cast<s64>(instance.GetTotalMemoryBudget());
    const s64 min_spacing_expected = device_local_memory - 1_GB;
    const s64 min_spacing_critical = device_local_memory - 512_MB;
    const s64 mem_threshold = std::min<s64>(device_local_memory, TARGET_GC_THRESHOLD);
    const s64 min_vacancy_expected = (6 * mem_threshold) / 10;
    const s64 min_vacancy_critical = (2 * mem_threshold) / 10;
    trigger_gc_memory = static_cast<u64>(
        std::max<u64>(std::min(device_local_memory - min_vacancy_expected, min_spacing_expected),
                      DEFAULT_TRIGGER_GC_MEMORY));
    critical_gc_memory = static_cast<u64>(
        std::max<u64>(std::min(device_local_memory - min_vacancy_critical, min_spacing_critical),
                      DEFAULT_CRITICAL_GC_MEMORY));
}

BufferCache::~BufferCache() = default;

void BufferCache::InvalidateMemory(VAddr device_addr, u64 size) {
    if (!IsRegionRegistered(device_addr, size)) {
        return;
    }
    memory_tracker->InvalidateRegion(
        device_addr, size, [this, device_addr, size] { ReadMemory(device_addr, size, true); });
}

void BufferCache::ReadMemory(VAddr device_addr, u64 size, bool is_write) {
    if (!is_write && TryServeFromFlushedWriteback(device_addr, size)) {
        return;
    }
    liverpool->SendCommand<true>([this, device_addr, size, is_write] {
        if (!is_write && ServeReadFromWriteback(device_addr, size)) {
            return;
        }
        Buffer& buffer = slot_buffers[FindBuffer(device_addr, size)];
        DownloadBufferMemory<false>(buffer, device_addr, size, is_write);
    });
}

// The common poll follows a fence whose writeback CommitPendingWriteback already recorded and
// submitted; the guest thread only has to wait for the priority delivery to land. Blocking here
// instead of on the GPU thread keeps the command processor recording the next frame while the
// game waits out the fence it was shown early.  :async-buffer-writeback
bool BufferCache::TryServeFromFlushedWriteback(VAddr device_addr, u64 size) {
    if (!async_writeback ||
        EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled) {
        return false;
    }
    const VAddr end = device_addr + size;
    std::unique_lock lk{writeback_mutex};
    u64 wait_tick = 0;
    bool covered = false;
    for (const auto& batch : pending_writebacks) {
        // A batch on the current tick has not been submitted; only the GPU thread may flush, so
        // reads it covers fall back to the command-stream serve.
        if (batch.tick >= scheduler.CurrentTick()) {
            continue;
        }
        for (const auto& wb : batch.writebacks) {
            if (wb.device_addr < end && device_addr < wb.device_addr + wb.size) {
                covered = true;
                wait_tick = batch.tick;
                break;
            }
        }
    }
    if (!covered) {
        return false;
    }
    // Delivery pops batches front to back, so ours has landed once the front tick moves past it.
    writeback_cv.wait(lk, [&] {
        return pending_writebacks.empty() || pending_writebacks.front().tick > wait_tick;
    });
    return true;
}

// A guarded poll follows a fence the guest was shown at command-processing time, so the write it
// wants may still be sitting unsubmitted in the current frame. Recording the writeback right now
// and waiting out that one tick serves the read with the exact post-fence value, and delivering
// the whole batch unguards every other control block with it -- one GPU wait covers the frame's
// polls, where a download would pay a full drain for each.  :async-buffer-writeback
bool BufferCache::ServeReadFromWriteback(VAddr device_addr, u64 size) {
    if (!async_writeback) {
        return false;
    }
    DeliverCompletedWritebacks();
    RecordGpuWriteback();
    const VAddr end = device_addr + size;
    u64 wait_tick = 0;
    bool covered = false;
    {
        std::scoped_lock lk{writeback_mutex};
        for (const auto& batch : pending_writebacks) {
            for (const auto& wb : batch.writebacks) {
                if (wb.device_addr < end && device_addr < wb.device_addr + wb.size) {
                    covered = true;
                    wait_tick = batch.tick;
                    break;
                }
            }
        }
    }
    if (!covered) {
        // A word the guest wrote itself never comes back from the GPU; its own write is the
        // value, so the guard has nothing left to hold the read for.
        if (memory_tracker->IsRegionCpuModified(device_addr, size)) {
            memory_tracker->UnmarkRegionAsGpuModified(device_addr, size);
            return true;
        }
        return false;
    }
    scheduler.Wait(wait_tick);
    DeliverCompletedWritebacks();
    return true;
}

template <bool async>
void BufferCache::DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size, bool is_write) {
    boost::container::small_vector<vk::BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    memory_tracker->ForEachDownloadRange<false>(
        device_addr, size, [&](u64 device_addr_out, u64 range_size) {
            const VAddr buffer_addr = buffer.CpuAddr();
            const auto add_download = [&](VAddr start, VAddr end) {
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
            };
            gpu_modified_ranges.ForEachInRange(device_addr_out, range_size, add_download);
            gpu_modified_ranges.Subtract(device_addr_out, range_size);
        });
    if (total_size_bytes == 0) {
        return;
    }
    const auto [download, offset] = download_buffer.Map(total_size_bytes);
    if (download == nullptr) {
        // Recording the copy against an allocation that was never made has the GPU write past the
        // end of the download buffer, which loses the device. Drop the download instead; the
        // region stays marked, so a later read faults again and retries.
        LOG_WARNING(Render_Vulkan, "Skipped {} byte download, staging buffer could not satisfy it",
                    total_size_bytes);
        return;
    }
    for (auto& copy : copies) {
        // Modify copies to have the staging offset in mind
        copy.dstOffset += offset;
    }
    download_buffer.Commit();
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(buffer.buffer, download_buffer.Handle(), copies);
    // Captured by value: when deferred, this outlives the frame that recorded the copy.
    const auto write_data = [this, copies, download, offset, buffer_addr = buffer.CpuAddr(),
                             device_addr, size, is_write] {
        for (const auto& copy : copies) {
            const VAddr copy_device_addr = buffer_addr + copy.srcOffset;
            const u64 dst_offset = copy.dstOffset - offset;
            memory->TryWriteBacking(std::bit_cast<u8*>(copy_device_addr), download + dst_offset,
                                    copy.size);
        }
        memory_tracker->UnmarkRegionAsGpuModified(device_addr, size);
        if (is_write) {
            memory_tracker->MarkRegionAsCpuModified(device_addr, size);
        }
    };
    if constexpr (async) {
        scheduler.DeferOperation(write_data);
    } else {
        scheduler.Finish();
        write_data();
    }
}

void BufferCache::MarkGpuWritten(VAddr device_addr, u64 size) {
    gpu_modified_ranges.Add(device_addr, size);
    // Judge the size of this write on its own, before the range set coalesces it with its
    // neighbours. Control blocks the guest polls tend to sit right next to the bulk output they
    // describe, so once merged a 256 byte counter block is indistinguishable from the megabytes of
    // particle vertices beside it, and filtering after the merge throws out both.
    if (!async_writeback) {
        return;
    }
    // Only the small control blocks are carried back. Bulk output is consumed by the GPU alone
    // -- Second Son's particles run correctly off the guarded blocks with none of it delivered
    // -- and carrying it copied tens of megabytes per frame the guest never read.
    if (size <= WritebackWriteLimit) {
        gpu_written_ranges.Add(device_addr, size);
    }
}

// Guest code may only look at GPU produced data after a fence, so handing it over one submit late
// is still in time. Doing that here rather than from a read fault keeps the transfer off the guest
// thread entirely, which is what makes readback affordable with the fault paths disabled: games
// that feed GPU results back into their own logic (auto exposure, particle counters) see real
// values instead of zeros.  :async-buffer-writeback
void BufferCache::WritebackGpuData() {
    // Precise mode services reads from the fault path, which owns the invalidation rules and would
    // race this one; leave it alone.
    async_writeback = EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Precise;
    if (!async_writeback) {
        gpu_written_ranges.Clear();
        {
            std::scoped_lock lk{writeback_mutex};
            pending_writebacks.clear();
        }
        writeback_cv.notify_all();
        return;
    }
    DeliverCompletedWritebacks();
    RecordGpuWriteback();
}

// The guest is shown its fence at command-processing time, far ahead of the real GPU; the poll
// that follows would fault and stall the whole command processor waiting the tick out. Submitting
// the control-block writeback right at the fence instead lets the GPU start on it immediately and
// the priority thread deliver it the moment it completes -- the poll then blocks only the guest
// thread, and only for the remainder of the actual GPU work.  :async-buffer-writeback
void BufferCache::CommitPendingWriteback() {
    if (!async_writeback ||
        EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled) {
        return;
    }
    DeliverCompletedWritebacks();
    if (RecordGpuWriteback()) {
        scheduler.Flush();
    }
}

void BufferCache::DeliverCompletedWritebacks() {
    std::unique_lock lk{writeback_mutex};
    bool popped = false;
    while (!pending_writebacks.empty() && scheduler.IsFree(pending_writebacks.front().tick)) {
        for (const auto& [device_addr, staging_offset, size] :
             pending_writebacks.front().writebacks) {
            // The guest writing a page itself outranks anything the GPU produced there; delivering
            // over it would lose the write. Skip only those pages rather than the whole range: a
            // range can easily cover both a block the guest rewrites every frame and the control
            // block beside it that it only ever reads, and dropping both leaves the second stale.
            // A newer GPU write is not a reason to skip. This memory *is* the GPU output on real
            // hardware, so an older complete snapshot is closer to the truth than what is there
            // now, and the next batch corrects it.
            const u8* const staged = writeback_buffer.mapped_data.data() + staging_offset;
            const VAddr range_end = device_addr + size;
            VAddr cursor = device_addr;
            const auto deliver_upto = [&](VAddr stop) {
                if (stop > cursor) {
                    memory->TryWriteBacking(std::bit_cast<u8*>(cursor),
                                            staged + (cursor - device_addr), stop - cursor);
                }
            };
            memory_tracker->ForEachCpuModifiedRange(
                device_addr, size, [&](VAddr owned_addr, u64 owned_size) {
                    const VAddr owned_end = std::min(owned_addr + owned_size, range_end);
                    if (owned_end <= cursor) {
                        return;
                    }
                    deliver_upto(std::min(owned_addr, range_end));
                    cursor = owned_end;
                });
            deliver_upto(range_end);
            // Dropping the guard here, batch-wide, is what keeps the fault rate down: the first
            // poll of a frame pays the wait, and every other control block the batch carried
            // reads on without faulting. Guest-written parts count as served too -- the guest's
            // own value outranks the snapshot.
            memory_tracker->UnmarkRegionAsGpuModified(device_addr, size);
        }
        pending_writebacks.pop_front();
        popped = true;
    }
    lk.unlock();
    if (popped) {
        writeback_cv.notify_all();
    }
}

bool BufferCache::RecordGpuWriteback() {
    // A range we cannot take right now stays queued for the next submit. Discarding it because the
    // ring or the batch list happened to be busy loses the only copy of that GPU output, and for
    // anything the guest polls every frame that reads as the value never having been produced.
    {
        std::scoped_lock lk{writeback_mutex};
        if (pending_writebacks.size() >= MaxWritebackBatches) {
            return false;
        }
    }

    WritebackBatch batch;
    boost::container::small_vector<std::pair<vk::Buffer, vk::BufferCopy>, 16> planned;
    boost::container::small_vector<std::pair<VAddr, u64>, 16> settled;
    u64 budget = WritebackBudget;

    gpu_written_ranges.ForEach([&](VAddr start, VAddr end) {
        // A merged run can span several cache buffers -- bulk output sits right next to the header
        // block describing it -- and judging the whole run by the buffer under its first page
        // throws away every byte of it, every submit. Walk the page table instead and take each
        // buffer-resident piece on its own.
        VAddr cursor = start;
        while (cursor < end) {
            const BufferId buffer_id = page_table[cursor >> CACHING_PAGEBITS].buffer_id;
            VAddr run_end = std::min(Common::AlignUp(cursor + 1, CACHING_PAGESIZE), end);
            while (run_end < end && page_table[run_end >> CACHING_PAGEBITS].buffer_id == buffer_id) {
                run_end = std::min(run_end + CACHING_PAGESIZE, end);
            }
            const u64 size = run_end - cursor;
            const VAddr run_start = cursor;
            cursor = run_end;
            if (IsBufferInvalid(buffer_id)) {
                // Nothing to copy from, so requeueing this would only spin on it every submit.
                settled.emplace_back(run_start, size);
                continue;
            }
            Buffer& buffer = slot_buffers[buffer_id];
            if (!buffer.IsInBounds(run_start, size)) {
                settled.emplace_back(run_start, size);
                continue;
            }
            // Left queued rather than settled: what does not fit this submit's budget is real
            // data and is picked up by a later submit.
            if (size > budget) {
                continue;
            }
            // Never stall the GPU thread on the ring; a skipped range is picked up next submit.
            const auto [mapped, offset] = writeback_buffer.Map(size, 64, false);
            if (mapped == nullptr) {
                continue;
            }
            writeback_buffer.Commit();
            planned.emplace_back(buffer.Handle(), vk::BufferCopy{
                                                      .srcOffset = buffer.Offset(run_start),
                                                      .dstOffset = offset,
                                                      .size = size,
                                                  });
            batch.writebacks.push_back(GpuWriteback{run_start, offset, size});
            settled.emplace_back(run_start, size);
            budget -= size;
        }
    });

    for (const auto& [start, size] : settled) {
        gpu_written_ranges.Subtract(start, size);
    }

    if (planned.empty()) {
        return false;
    }
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    const vk::MemoryBarrier2 barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
    // Ranges arrive in address order, so runs sharing a source buffer are already adjacent.
    boost::container::small_vector<vk::BufferCopy, 16> copies;
    for (size_t i = 0; i < planned.size(); ++i) {
        copies.push_back(planned[i].second);
        if (i + 1 == planned.size() || planned[i + 1].first != planned[i].first) {
            cmdbuf.copyBuffer(planned[i].first, writeback_buffer.Handle(), copies);
            copies.clear();
        }
    }
    batch.tick = scheduler.CurrentTick();
    {
        std::scoped_lock lk{writeback_mutex};
        pending_writebacks.push_back(std::move(batch));
    }
    // Queued before the flush that submits this tick so the priority thread delivers the moment
    // the GPU completes it; a guarded poll then usually finds its page already served.
    scheduler.DeferPriorityOperation([this] { DeliverCompletedWritebacks(); });
    return true;
}

void BufferCache::BindVertexBuffers(
    const Vulkan::GraphicsPipeline& pipeline,
    boost::container::small_vector<vk::BufferMemoryBarrier2, 16>& barriers) {
    const auto& regs = liverpool->regs;
    Vulkan::VertexInputs<vk::VertexInputAttributeDescription2EXT> attributes;
    Vulkan::VertexInputs<vk::VertexInputBindingDescription2EXT> bindings;
    Vulkan::VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT> divisors;
    Vulkan::VertexInputs<AmdGpu::Buffer> guest_buffers;
    pipeline.GetVertexInputs(attributes, bindings, divisors, guest_buffers,
                             regs.vgt_instance_step_rate_0, regs.vgt_instance_step_rate_1);

    if (instance.IsVertexInputDynamicState()) {
        // Update current vertex inputs.
        const auto cmdbuf = scheduler.CommandBuffer();
        cmdbuf.setVertexInputEXT(bindings, attributes);
    }

    if (bindings.empty()) {
        // If there are no bindings, there is nothing further to do.
        return;
    }

    struct BufferRange {
        VAddr base_address;
        VAddr end_address;
        vk::Buffer vk_buffer;
        u64 offset;

        [[nodiscard]] size_t GetSize() const {
            return end_address - base_address;
        }
    };

    // Build list of ranges covering the requested buffers
    Vulkan::VertexInputs<BufferRange> ranges{};
    for (const auto& buffer : guest_buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            ranges.emplace_back(buffer.base_address, buffer.base_address + buffer.GetSize());
        }
    }

    // Merge connecting ranges together
    Vulkan::VertexInputs<BufferRange> ranges_merged{};
    if (!ranges.empty()) {
        std::ranges::sort(ranges, [](const BufferRange& lhv, const BufferRange& rhv) {
            return lhv.base_address < rhv.base_address;
        });
        ranges_merged.emplace_back(ranges[0]);
        for (auto range : ranges) {
            auto& prev_range = ranges_merged.back();
            if (prev_range.end_address < range.base_address) {
                ranges_merged.emplace_back(range);
            } else {
                prev_range.end_address = std::max(prev_range.end_address, range.end_address);
            }
        }
    }

    // Map buffers for merged ranges
    for (auto& range : ranges_merged) {
        const u64 size = memory->ClampRangeSize(range.base_address, range.GetSize());
        const auto [buffer, offset] = ObtainBuffer(range.base_address, size, false);
        range.vk_buffer = buffer->buffer;
        range.offset = offset;
        if (IsRegionGpuModified(range.base_address, size)) {
            if (auto barrier =
                    buffer->GetBarrier(vk::AccessFlagBits2::eVertexAttributeRead,
                                       vk::PipelineStageFlagBits2::eVertexAttributeInput)) {
                barriers.emplace_back(*barrier);
            }
        }
    }

    // Bind vertex buffers
    Vulkan::VertexInputs<vk::Buffer> host_buffers;
    Vulkan::VertexInputs<vk::DeviceSize> host_offsets;
    Vulkan::VertexInputs<vk::DeviceSize> host_sizes;
    Vulkan::VertexInputs<vk::DeviceSize> host_strides;
    for (const auto& buffer : guest_buffers) {
        if (buffer.base_address != 0 && buffer.GetSize() > 0) {
            const auto host_buffer_info =
                std::ranges::find_if(ranges_merged, [&](const BufferRange& range) {
                    return buffer.base_address >= range.base_address &&
                           buffer.base_address < range.end_address;
                });
            ASSERT(host_buffer_info != ranges_merged.cend());
            host_buffers.emplace_back(host_buffer_info->vk_buffer);
            host_offsets.push_back(host_buffer_info->offset + buffer.base_address -
                                   host_buffer_info->base_address);
        } else {
            host_buffers.emplace_back(VK_NULL_HANDLE);
            host_offsets.push_back(0);
        }
        host_sizes.push_back(buffer.GetSize());
        host_strides.push_back(buffer.GetStride());
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    const auto num_buffers = guest_buffers.size();
    if (instance.IsVertexInputDynamicState()) {
        cmdbuf.bindVertexBuffers(0, num_buffers, host_buffers.data(), host_offsets.data());
    } else {
        cmdbuf.bindVertexBuffers2(0, num_buffers, host_buffers.data(), host_offsets.data(),
                                  host_sizes.data(), host_strides.data());
    }
}

void BufferCache::BindIndexBuffer(
    u32 index_offset, boost::container::small_vector<vk::BufferMemoryBarrier2, 16>& barriers) {
    const auto& regs = liverpool->regs;

    // Figure out index type and size.
    const bool is_index16 = regs.index_buffer_type.index_type == AmdGpu::IndexType::Index16;
    const vk::IndexType index_type = is_index16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const u32 index_size = is_index16 ? sizeof(u16) : sizeof(u32);
    const VAddr index_address =
        regs.index_base_address.Address<VAddr>() + index_offset * index_size;

    // Bind index buffer.
    const u32 index_buffer_size = regs.num_indices * index_size;
    const auto [vk_buffer, offset] = ObtainBuffer(index_address, index_buffer_size, false);
    if (IsRegionGpuModified(index_address, index_buffer_size)) {
        if (auto barrier = vk_buffer->GetBarrier(vk::AccessFlagBits2::eIndexRead,
                                                 vk::PipelineStageFlagBits2::eIndexInput)) {
            barriers.emplace_back(*barrier);
        }
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.bindIndexBuffer(vk_buffer->Handle(), offset, index_type);
}

void BufferCache::FillBuffer(VAddr address, u32 num_bytes, u32 value, bool is_gds) {
    ASSERT_MSG(address % 4 == 0, "GDS offset must be dword aligned");
    if (!is_gds) {
        texture_cache.ClearMeta(address);
        if (!IsRegionGpuModified(address, num_bytes)) {
            u32* buffer = std::bit_cast<u32*>(address);
            std::fill(buffer, buffer + num_bytes / sizeof(u32), value);
            return;
        }
    }
    Buffer* buffer = [&] {
        if (is_gds) {
            return &gds_buffer;
        }
        const auto [buffer, offset] = ObtainBuffer(address, num_bytes, true);
        return buffer;
    }();
    buffer->Fill(buffer->Offset(address), num_bytes, value);
}

void BufferCache::CopyBuffer(VAddr dst, VAddr src, u32 num_bytes, bool dst_gds, bool src_gds) {
    if (!dst_gds && !IsRegionGpuModified(dst, num_bytes)) {
        if (!src_gds && !IsRegionGpuModified(src, num_bytes) &&
            !texture_cache.FindImageFromRange(src, num_bytes)) {
            // Both buffers were not transferred to GPU yet. Can safely copy in host memory.
            memcpy(std::bit_cast<void*>(dst), std::bit_cast<void*>(src), num_bytes);
            return;
        }
        // Without a readback there's nothing we can do with this
        // Fallback to creating dst buffer on GPU to at least have this data there
    }
    texture_cache.InvalidateMemoryFromGPU(dst, num_bytes);
    auto& src_buffer = [&] -> const Buffer& {
        if (src_gds) {
            return gds_buffer;
        }
        const auto buffer_id = FindBuffer(src, num_bytes);
        auto& buffer = slot_buffers[buffer_id];
        SynchronizeBuffer(buffer, src, num_bytes, false, true);
        return buffer;
    }();
    auto& dst_buffer = [&] -> const Buffer& {
        if (dst_gds) {
            return gds_buffer;
        }
        const auto buffer_id = FindBuffer(dst, num_bytes);
        auto& buffer = slot_buffers[buffer_id];
        SynchronizeBuffer(buffer, dst, num_bytes, true, true);
        MarkGpuWritten(dst, num_bytes);
        return buffer;
    }();
    const vk::BufferCopy region = {
        .srcOffset = src_buffer.Offset(src),
        .dstOffset = dst_buffer.Offset(dst),
        .size = num_bytes,
    };
    const vk::BufferMemoryBarrier2 buf_barriers_before[2] = {
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .buffer = dst_buffer.Handle(),
            .offset = dst_buffer.Offset(dst),
            .size = num_bytes,
        },
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .buffer = src_buffer.Handle(),
            .offset = src_buffer.Offset(src),
            .size = num_bytes,
        },
    };
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = buf_barriers_before,
    });
    cmdbuf.copyBuffer(src_buffer.Handle(), dst_buffer.Handle(), region);
    const vk::BufferMemoryBarrier2 buf_barriers_after[2] = {
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
            .buffer = dst_buffer.Handle(),
            .offset = dst_buffer.Offset(dst),
            .size = num_bytes,
        },
        {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .buffer = src_buffer.Handle(),
            .offset = src_buffer.Offset(src),
            .size = num_bytes,
        },
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = buf_barriers_after,
    });
}

std::pair<Buffer*, u32> BufferCache::ObtainBuffer(VAddr device_addr, u32 size, bool is_written,
                                                  bool is_texel_buffer, BufferId buffer_id) {
    // For read-only buffers use device local stream buffer to reduce renderpass breaks.
    if (!is_written && size <= CACHING_PAGESIZE && !IsRegionGpuModified(device_addr, size)) {
        const u64 offset = stream_buffer.Copy(device_addr, size, instance.UniformMinAlignment());
        return {&stream_buffer, offset};
    }
    if (IsBufferInvalid(buffer_id)) {
        buffer_id = FindBuffer(device_addr, size);
    }
    Buffer& buffer = slot_buffers[buffer_id];
    SynchronizeBuffer(buffer, device_addr, size, is_written, is_texel_buffer);
    if (is_written) {
        MarkGpuWritten(device_addr, size);
    }
    return {&buffer, buffer.Offset(device_addr)};
}

std::pair<Buffer*, u32> BufferCache::ObtainBufferForImage(VAddr gpu_addr, u32 size) {
    // Check if any buffer contains the full requested range.
    const BufferId buffer_id = page_table[gpu_addr >> CACHING_PAGEBITS].buffer_id;
    if (buffer_id) {
        if (Buffer& buffer = slot_buffers[buffer_id]; buffer.IsInBounds(gpu_addr, size)) {
            SynchronizeBuffer(buffer, gpu_addr, size, false, false);
            return {&buffer, buffer.Offset(gpu_addr)};
        }
    }
    // If some buffer within was GPU modified create a full buffer to avoid losing GPU data.
    if (IsRegionGpuModified(gpu_addr, size)) {
        return ObtainBuffer(gpu_addr, size, false, false);
    }
    // In all other cases, just do a CPU copy to the staging buffer.
    const auto [data, offset] = staging_buffer.Map(size, 16);
    memory->CopySparseMemory(gpu_addr, data, size);
    staging_buffer.Commit();
    return {&staging_buffer, offset};
}

bool BufferCache::IsRegionRegistered(VAddr addr, size_t size) {
    // Check if we are missing some edge case here
    return buffer_ranges.Intersects(addr, size);
}

bool BufferCache::IsRegionCpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionCpuModified(addr, size);
}

bool BufferCache::IsRegionGpuModified(VAddr addr, size_t size) {
    return memory_tracker->IsRegionGpuModified(addr, size);
}

BufferId BufferCache::FindBuffer(VAddr device_addr, u32 size) {
    ASSERT(device_addr != 0);
    const u64 page = device_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = page_table[page].buffer_id;
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
        const BufferId overlap_id = page_table[device_addr >> CACHING_PAGEBITS].buffer_id;
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
                expand_end(CACHING_PAGESIZE * 128);
            }
            if (expands_left) {
                expand_begin(CACHING_PAGESIZE * 128);
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

    boost::container::static_vector<vk::BufferMemoryBarrier2, 2> pre_barriers{};
    if (auto src_barrier = overlap.GetBarrier(vk::AccessFlagBits2::eTransferRead,
                                              vk::PipelineStageFlagBits2::eTransfer)) {
        pre_barriers.push_back(*src_barrier);
    }
    if (auto dst_barrier =
            new_buffer.GetBarrier(vk::AccessFlagBits2::eTransferWrite,
                                  vk::PipelineStageFlagBits2::eTransfer, dst_base_offset)) {
        pre_barriers.push_back(*dst_barrier);
    }
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<u32>(pre_barriers.size()),
        .pBufferMemoryBarriers = pre_barriers.data(),
    });

    cmdbuf.copyBuffer(overlap.Handle(), new_buffer.Handle(), copy);

    boost::container::static_vector<vk::BufferMemoryBarrier2, 2> post_barriers{};
    if (auto src_barrier =
            overlap.GetBarrier(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                               vk::PipelineStageFlagBits2::eAllCommands)) {
        post_barriers.push_back(*src_barrier);
    }
    if (auto dst_barrier = new_buffer.GetBarrier(
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eAllCommands, dst_base_offset)) {
        post_barriers.push_back(*dst_barrier);
    }
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<u32>(post_barriers.size()),
        .pBufferMemoryBarriers = post_barriers.data(),
    });
    DeleteBuffer(overlap_id);
}

BufferId BufferCache::CreateBuffer(VAddr device_addr, u32 wanted_size) {
    const VAddr device_addr_end = Common::AlignUp(device_addr + wanted_size, CACHING_PAGESIZE);
    device_addr = Common::AlignDown(device_addr, CACHING_PAGESIZE);
    wanted_size = static_cast<u32>(device_addr_end - device_addr);
    const OverlapResult overlap = ResolveOverlaps(device_addr, wanted_size);
    const u32 size = static_cast<u32>(overlap.end - overlap.begin);
    const BufferId new_buffer_id =
        slot_buffers.insert(instance, scheduler, MemoryUsage::DeviceLocal, overlap.begin,
                            AllFlags | vk::BufferUsageFlagBits::eShaderDeviceAddress, size);
    auto& new_buffer = slot_buffers[new_buffer_id];
    for (const BufferId overlap_id : overlap.ids) {
        JoinOverlap(new_buffer_id, overlap_id, !overlap.has_stream_leap);
    }
    Register(new_buffer_id);
    return new_buffer_id;
}

void BufferCache::ProcessFaultBuffer() {
    fault_manager.ProcessFaultBuffer();
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
    const u64 size_pages = page_end - page_begin;
    for (u64 page = page_begin; page != page_end; ++page) {
        if constexpr (insert) {
            page_table[page].buffer_id = buffer_id;
        } else {
            page_table[page].buffer_id = BufferId{};
        }
    }
    if constexpr (insert) {
        total_used_memory += Common::AlignUp(size, CACHING_PAGESIZE);
        buffer.SetLRUId(lru_cache.Insert(buffer_id, gc_tick));
        boost::container::small_vector<vk::DeviceAddress, 128> bda_addrs;
        bda_addrs.reserve(size_pages);
        for (u64 i = 0; i < size_pages; ++i) {
            vk::DeviceAddress addr = buffer.BufferDeviceAddress() + (i << CACHING_PAGEBITS);
            bda_addrs.push_back(addr);
        }
        WriteDataBuffer(bda_pagetable_buffer, page_begin * sizeof(vk::DeviceAddress),
                        bda_addrs.data(), bda_addrs.size() * sizeof(vk::DeviceAddress));
        buffer_ranges.Add(buffer.CpuAddr(), buffer.SizeBytes(), buffer_id);
    } else {
        total_used_memory -= Common::AlignUp(size, CACHING_PAGESIZE);
        lru_cache.Free(buffer.LRUId());
        const u64 offset = bda_pagetable_buffer.Offset(page_begin * sizeof(vk::DeviceAddress));
        bda_pagetable_buffer.Fill(offset, size_pages * sizeof(vk::DeviceAddress), 0);
        buffer_ranges.Subtract(buffer.CpuAddr(), buffer.SizeBytes());
    }
}

bool BufferCache::SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size, bool is_written,
                                    bool is_texel_buffer) {
    // Every bind counts as a use. Touching only on upload would leave a buffer the guest keeps
    // binding but never rewrites looking untouched since creation, and the collector would take it
    // out from under the frame using it.
    TouchBuffer(buffer);
    boost::container::small_vector<vk::BufferCopy, 4> copies;
    size_t total_size_bytes = 0;
    VAddr buffer_start = buffer.CpuAddr();
    vk::Buffer src_buffer = VK_NULL_HANDLE;
    // Small written bindings are the control blocks readback consumers poll (counters, emitter
    // headers); with readbacks disabled, guard them so a guest read faults and is served exact
    // data at read time. Bulk output stays unguarded and reaches the guest through the async
    // writeback instead.
    const bool guard = is_written && size <= WritebackWriteLimit &&
                       EmulatorSettings.GetReadbacksMode() == GpuReadbacksMode::Disabled;
    memory_tracker->ForEachUploadRange(
        device_addr, size, is_written, guard,
        [&](u64 device_addr_out, u64 range_size) {
            copies.emplace_back(total_size_bytes, device_addr_out - buffer_start, range_size);
            total_size_bytes += range_size;
        },
        [&] { src_buffer = UploadCopies(buffer, copies, total_size_bytes); });

    if (src_buffer) {
        scheduler.EndRendering();
        const auto cmdbuf = scheduler.CommandBuffer();
        const vk::BufferMemoryBarrier2 pre_barrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
                             vk::AccessFlagBits2::eTransferRead |
                             vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .buffer = buffer.Handle(),
            .offset = 0,
            .size = buffer.SizeBytes(),
        };
        const vk::BufferMemoryBarrier2 post_barrier = {
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .buffer = buffer.Handle(),
            .offset = 0,
            .size = buffer.SizeBytes(),
        };
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &pre_barrier,
        });
        cmdbuf.copyBuffer(src_buffer, buffer.buffer, copies);
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &post_barrier,
        });
    }
    if (is_texel_buffer && !is_written) {
        return SynchronizeBufferFromImage(buffer, device_addr, size);
    }
    return false;
}

vk::Buffer BufferCache::UploadCopies(Buffer& buffer, std::span<vk::BufferCopy> copies,
                                     size_t total_size_bytes) {
    if (copies.empty()) {
        return VK_NULL_HANDLE;
    }
    const auto [staging, offset] = staging_buffer.Map(total_size_bytes);
    if (staging) {
        for (auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            memory->CopySparseMemory(device_addr, src_pointer, copy.size);
            // Apply the staging offset
            copy.srcOffset += offset;
        }
        staging_buffer.Commit();
        return staging_buffer.Handle();
    } else {
        // For large one time transfers use a temporary host buffer.
        auto temp_buffer =
            std::make_unique<Buffer>(instance, scheduler, MemoryUsage::Upload, 0,
                                     vk::BufferUsageFlagBits::eTransferSrc, total_size_bytes);
        const vk::Buffer src_buffer = temp_buffer->Handle();
        u8* const staging = temp_buffer->mapped_data.data();
        for (const auto& copy : copies) {
            u8* const src_pointer = staging + copy.srcOffset;
            const VAddr device_addr = buffer.CpuAddr() + copy.dstOffset;
            memory->CopySparseMemory(device_addr, src_pointer, copy.size);
        }
        scheduler.DeferOperation([buffer = std::move(temp_buffer)]() mutable { buffer.reset(); });
        return src_buffer;
    }
}

bool BufferCache::SynchronizeBufferFromImage(Buffer& buffer, VAddr device_addr, u32 size) {
    const ImageId image_id = texture_cache.FindImageFromRange(device_addr, size);
    if (!image_id) {
        return false;
    }
    Image& image = texture_cache.GetImage(image_id);
    ASSERT_MSG(device_addr == image.info.guest_address,
               "Texel buffer aliases image subresources {:x} : {:x}", device_addr,
               image.info.guest_address);
    const u32 buf_offset = buffer.Offset(image.info.guest_address);
    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    u32 copy_size = 0;
    for (u32 mip = 0; mip < image.info.resources.levels; mip++) {
        const auto& mip_info = image.info.mips_layout[mip];
        const u32 width = std::max(image.info.size.width >> mip, 1u);
        const u32 height = std::max(image.info.size.height >> mip, 1u);
        const u32 depth = std::max(image.info.size.depth >> mip, 1u);
        if (buf_offset + mip_info.offset + mip_info.size > buffer.SizeBytes()) {
            break;
        }
        buffer_copies.push_back(vk::BufferImageCopy{
            .bufferOffset = mip_info.offset,
            .bufferRowLength = mip_info.pitch,
            .bufferImageHeight = mip_info.height,
            .imageSubresource{
                .aspectMask = image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = image.info.resources.layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, depth},
        });
        copy_size += mip_info.size;
    }
    if (copy_size == 0) {
        return false;
    }
    auto& tile_manager = texture_cache.GetTileManager();
    tile_manager.TileImage(image, buffer_copies, buffer.Handle(), buf_offset, copy_size);
    return true;
}

void BufferCache::SynchronizeBuffersInRange(VAddr device_addr, u64 size) {
    const VAddr device_addr_end = device_addr + size;
    ForEachBufferInRange(device_addr, size, [&](BufferId buffer_id, Buffer& buffer) {
        RENDERER_TRACE;
        VAddr start = std::max(buffer.CpuAddr(), device_addr);
        VAddr end = std::min(buffer.CpuAddr() + buffer.SizeBytes(), device_addr_end);
        u32 size = static_cast<u32>(end - start);
        SynchronizeBuffer(buffer, start, size, false, false);
    });
}

void BufferCache::WriteDataBuffer(Buffer& buffer, VAddr address, const void* value, u32 num_bytes) {
    vk::BufferCopy copy = {
        .srcOffset = 0,
        .dstOffset = buffer.Offset(address),
        .size = num_bytes,
    };
    vk::Buffer src_buffer = staging_buffer.Handle();
    if (num_bytes < StagingBufferSize) {
        const auto [staging, offset] = staging_buffer.Map(num_bytes);
        std::memcpy(staging, value, num_bytes);
        copy.srcOffset = offset;
        staging_buffer.Commit();
    } else {
        // For large one time transfers use a temporary host buffer.
        // RenderDoc can lag quite a bit if the stream buffer is too large.
        Buffer temp_buffer{
            instance, scheduler, MemoryUsage::Upload, 0, vk::BufferUsageFlagBits::eTransferSrc,
            num_bytes};
        src_buffer = temp_buffer.Handle();
        u8* const staging = temp_buffer.mapped_data.data();
        std::memcpy(staging, value, num_bytes);
        scheduler.DeferOperation([buffer = std::move(temp_buffer)]() mutable {});
    }
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();
    const vk::BufferMemoryBarrier2 pre_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .buffer = buffer.Handle(),
        .offset = buffer.Offset(address),
        .size = num_bytes,
    };
    const vk::BufferMemoryBarrier2 post_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .buffer = buffer.Handle(),
        .offset = buffer.Offset(address),
        .size = num_bytes,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_barrier,
    });
    cmdbuf.copyBuffer(src_buffer, buffer.Handle(), copy);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_barrier,
    });
}

void BufferCache::RunGarbageCollector() {
    SCOPE_EXIT {
        ++gc_tick;
    };
    if (instance.CanReportMemoryUsage()) {
        total_used_memory = instance.GetDeviceMemoryUsage();
    }
    if (total_used_memory < trigger_gc_memory) {
        return;
    }
    const bool aggressive = total_used_memory >= critical_gc_memory;
    const u64 ticks_to_destroy = std::min<u64>(aggressive ? 80 : 160, gc_tick);
    int max_deletions = aggressive ? 64 : 32;
    const auto clean_up = [&](BufferId buffer_id) {
        if (max_deletions == 0) {
            return true;
        }
        --max_deletions;
        Buffer& buffer = slot_buffers[buffer_id];
        DownloadBufferMemory<true>(buffer, buffer.CpuAddr(), buffer.SizeBytes(), true);
        DeleteBuffer(buffer_id);
        return false;
    };
    lru_cache.ForEachItemBelow(gc_tick - ticks_to_destroy, clean_up);
}

void BufferCache::TouchBuffer(const Buffer& buffer) {
    lru_cache.Touch(buffer.LRUId(), gc_tick);
}

void BufferCache::DeleteBuffer(BufferId buffer_id) {
    Buffer& buffer = slot_buffers[buffer_id];
    Unregister(buffer_id);
    scheduler.DeferOperation([this, buffer_id] { slot_buffers.erase(buffer_id); });
    buffer.is_deleted = true;
}

} // namespace VideoCore
