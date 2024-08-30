// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <boost/container/small_vector.hpp>
#include <boost/icl/interval_map.hpp>
#include <tsl/robin_map.h>
#include "common/div_ceil.h"
#include "common/slot_vector.h"
#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/multi_level_page_table.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Shader {
struct Info;
}

namespace VideoCore {

using BufferId = Common::SlotId;

static constexpr BufferId NULL_BUFFER_ID{0};

static constexpr u32 NUM_VERTEX_BUFFERS = 32;

class BufferCache {
public:
    static constexpr u32 CACHING_PAGEBITS = 12;
    static constexpr u64 CACHING_PAGESIZE = u64{1} << CACHING_PAGEBITS;
    static constexpr u64 DEVICE_PAGESIZE = 4_KB;

    struct Traits {
        using Entry = BufferId;
        static constexpr size_t AddressSpaceBits = 39;
        static constexpr size_t FirstLevelBits = 14;
        static constexpr size_t PageBits = CACHING_PAGEBITS;
    };
    using PageTable = MultiLevelPageTable<Traits>;

    struct OverlapResult {
        boost::container::small_vector<BufferId, 16> ids;
        VAddr begin;
        VAddr end;
        bool has_stream_leap = false;
    };

public:
    explicit BufferCache(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                         const AmdGpu::Liverpool* liverpool, PageManager& tracker);
    ~BufferCache();

    /// Invalidates any buffer in the logical page range.
    void InvalidateMemory(VAddr device_addr, u64 size);

    /// Binds host vertex buffers for the current draw.
    bool BindVertexBuffers(const Shader::Info& vs_info);

    /// Bind host index buffer for the current draw.
    u32 BindIndexBuffer(bool& is_indexed, u32 index_offset);

    /// Obtains a buffer for the specified region.
    [[nodiscard]] std::pair<Buffer*, u32> ObtainBuffer(VAddr gpu_addr, u32 size, bool is_written,
                                                       bool is_texel_buffer = false);

    /// Obtains a temporary buffer for usage in texture cache.
    [[nodiscard]] std::pair<const Buffer*, u32> ObtainTempBuffer(VAddr gpu_addr, u32 size);

    /// Return true when a region is registered on the cache
    [[nodiscard]] bool IsRegionRegistered(VAddr addr, size_t size);

    /// Return true when a CPU region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr addr, size_t size);

    /// Return true when a CPU region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr addr, size_t size);

private:
    template <typename Func>
    void ForEachBufferInRange(VAddr device_addr, u64 size, Func&& func) {
        const u64 page_end = Common::DivCeil(device_addr + size, CACHING_PAGESIZE);
        for (u64 page = device_addr >> CACHING_PAGEBITS; page < page_end;) {
            const BufferId buffer_id = page_table[page];
            if (!buffer_id) {
                ++page;
                continue;
            }
            Buffer& buffer = slot_buffers[buffer_id];
            func(buffer_id, buffer);

            const VAddr end_addr = buffer.CpuAddr() + buffer.SizeBytes();
            page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
        }
    }

    void DownloadBufferMemory(Buffer& buffer, VAddr device_addr, u64 size);

    [[nodiscard]] BufferId FindBuffer(VAddr device_addr, u32 size);

    [[nodiscard]] OverlapResult ResolveOverlaps(VAddr device_addr, u32 wanted_size);

    void JoinOverlap(BufferId new_buffer_id, BufferId overlap_id, bool accumulate_stream_score);

    [[nodiscard]] BufferId CreateBuffer(VAddr device_addr, u32 wanted_size);

    void Register(BufferId buffer_id);

    void Unregister(BufferId buffer_id);

    template <bool insert>
    void ChangeRegister(BufferId buffer_id);

    bool SynchronizeBuffer(Buffer& buffer, VAddr device_addr, u32 size);

    void DeleteBuffer(BufferId buffer_id, bool do_not_mark = false);

    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    const AmdGpu::Liverpool* liverpool;
    PageManager& tracker;
    StreamBuffer staging_buffer;
    StreamBuffer stream_buffer;
    std::recursive_mutex mutex;
    Common::SlotVector<Buffer> slot_buffers;
    MemoryTracker memory_tracker;
    PageTable page_table;
};

} // namespace VideoCore
