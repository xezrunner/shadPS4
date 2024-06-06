// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#include "video_core/amdgpu/gpuaddr/gpuaddr_private.h"

uint64_t gpaComputeSurfaceAddrFromCoordLinear(
    uint32_t x,            ///< [in] x coord
    uint32_t y,            ///< [in] y coord
    uint32_t slice,        ///< [in] slice/depth index
    uint32_t sample,       ///< [in] sample index
    uint32_t bpp,          ///< [in] bits per pixel
    uint32_t pitch,        ///< [in] pitch
    uint32_t height,       ///< [in] height
    uint32_t numSlices,    ///< [in] number of slices
    uint32_t* pBitPosition ///< [out] bit position inside a byte
) {
    const uint64_t sliceSize = (uint64_t)pitch * height;

    uint64_t sliceOffset = (slice + sample * numSlices) * sliceSize;
    uint64_t rowOffset = (uint64_t)y * pitch;
    uint64_t pixOffset = x;

    uint64_t addr = (sliceOffset + rowOffset + pixOffset) * bpp;

    if (pBitPosition) {
        *pBitPosition = (uint32_t)(addr % 8);
    }
    addr /= 8;

    return addr;
}

GpaError gpaCalcSurfaceSizeOffset(uint64_t* outsize, uint64_t* outoffset, const GpaTextureInfo* tex,
                                  uint32_t miplevel, uint32_t arrayslice) {
    if (!tex) {
        return GPA_ERR_INVALID_ARGS;
    }

    const uint32_t numarrayslices = tex->numslices;
    const uint32_t basewidth = tex->width;
    const uint32_t baseheight = tex->height;
    const uint32_t basedepth = tex->depth;
    const uint32_t basepitch = tex->pitch;

    GpaTilingParams tp = {};
    GpaError res = gpaTpInit(&tp, tex, 0, arrayslice);
    if (res != GPA_ERR_OK) {
        return res;
    }

    GpaSurfaceInfo si = {0};

    uint32_t finaloffset = 0;
    uint32_t finalsize = 0;

    for (uint32_t m = 0; m <= miplevel; m += 1) {
        finaloffset += numarrayslices * finalsize;

        tp.linearwidth = std::max(basewidth >> m, 1U);
        tp.linearheight = std::max(baseheight >> m, 1U);
        tp.lineardepth = basedepth;
        tp.basetiledpitch = basepitch;
        tp.miplevel = m;

        res = gpaComputeSurfaceInfo(&si, &tp);
        if (res != GPA_ERR_OK) {
            return res;
        }

        finalsize = si.surfacesize;
    }

    finaloffset += si.surfacesize * arrayslice;

    if (outsize) {
        *outsize = finalsize;
    }
    if (outoffset) {
        *outoffset = finaloffset;
    }
    return GPA_ERR_OK;
}
