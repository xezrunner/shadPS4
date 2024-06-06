// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#pragma once

#include "common/types.h"

enum GnmSurfaceNumber {
    GNM_NUMBER_UNORM = 0x0,
    GNM_NUMBER_SNORM = 0x1,
    GNM_NUMBER_UINT = 0x4,
    GNM_NUMBER_SINT = 0x5,
    GNM_NUMBER_SRGB = 0x6,
    GNM_NUMBER_FLOAT = 0x7,
};

enum GnmImageFormat {
    GNM_IMG_DATA_FORMAT_INVALID = 0x0,
    GNM_IMG_DATA_FORMAT_8 = 0x1,
    GNM_IMG_DATA_FORMAT_16 = 0x2,
    GNM_IMG_DATA_FORMAT_8_8 = 0x3,
    GNM_IMG_DATA_FORMAT_32 = 0x4,
    GNM_IMG_DATA_FORMAT_16_16 = 0x5,
    GNM_IMG_DATA_FORMAT_10_11_11 = 0x6,
    GNM_IMG_DATA_FORMAT_11_11_10 = 0x7,
    GNM_IMG_DATA_FORMAT_10_10_10_2 = 0x8,
    GNM_IMG_DATA_FORMAT_2_10_10_10 = 0x9,
    GNM_IMG_DATA_FORMAT_8_8_8_8 = 0xa,
    GNM_IMG_DATA_FORMAT_32_32 = 0xb,
    GNM_IMG_DATA_FORMAT_16_16_16_16 = 0xc,
    GNM_IMG_DATA_FORMAT_32_32_32 = 0xd,
    GNM_IMG_DATA_FORMAT_32_32_32_32 = 0xe,
    GNM_IMG_DATA_FORMAT_5_6_5 = 0x10,
    GNM_IMG_DATA_FORMAT_1_5_5_5 = 0x11,
    GNM_IMG_DATA_FORMAT_5_5_5_1 = 0x12,
    GNM_IMG_DATA_FORMAT_4_4_4_4 = 0x13,
    GNM_IMG_DATA_FORMAT_8_24 = 0x14,
    GNM_IMG_DATA_FORMAT_24_8 = 0x15,
    GNM_IMG_DATA_FORMAT_X24_8_32 = 0x16,
    GNM_IMG_DATA_FORMAT_GB_GR = 0x20,
    GNM_IMG_DATA_FORMAT_BG_RG = 0x21,
    GNM_IMG_DATA_FORMAT_5_9_9_9 = 0x22,
    GNM_IMG_DATA_FORMAT_BC1 = 0x23,
    GNM_IMG_DATA_FORMAT_BC2 = 0x24,
    GNM_IMG_DATA_FORMAT_BC3 = 0x25,
    GNM_IMG_DATA_FORMAT_BC4 = 0x26,
    GNM_IMG_DATA_FORMAT_BC5 = 0x27,
    GNM_IMG_DATA_FORMAT_BC6 = 0x28,
    GNM_IMG_DATA_FORMAT_BC7 = 0x29,
    GNM_IMG_DATA_FORMAT_FMASK8_S2_F1 = 0x2c,
    GNM_IMG_DATA_FORMAT_FMASK8_S4_F1 = 0x2d,
    GNM_IMG_DATA_FORMAT_FMASK8_S8_F1 = 0x2e,
    GNM_IMG_DATA_FORMAT_FMASK8_S2_F2 = 0x2f,
    GNM_IMG_DATA_FORMAT_FMASK8_S4_F2 = 0x30,
    GNM_IMG_DATA_FORMAT_FMASK8_S4_F4 = 0x31,
    GNM_IMG_DATA_FORMAT_FMASK16_S16_F1 = 0x32,
    GNM_IMG_DATA_FORMAT_FMASK16_S8_F2 = 0x33,
    GNM_IMG_DATA_FORMAT_FMASK32_S16_F2 = 0x34,
    GNM_IMG_DATA_FORMAT_FMASK32_S8_F4 = 0x35,
    GNM_IMG_DATA_FORMAT_FMASK32_S8_F8 = 0x36,
    GNM_IMG_DATA_FORMAT_FMASK64_S16_F4 = 0x37,
    GNM_IMG_DATA_FORMAT_FMASK64_S16_F8 = 0x38,
    GNM_IMG_DATA_FORMAT_4_4 = 0x39,
    GNM_IMG_DATA_FORMAT_6_5_5 = 0x3a,
    GNM_IMG_DATA_FORMAT_1 = 0x3b,
    GNM_IMG_DATA_FORMAT_1_REVERSED = 0x3c,
    GNM_IMG_DATA_FORMAT_32_AS_8 = 0x3d,
    GNM_IMG_DATA_FORMAT_32_AS_8_8 = 0x3e,
    GNM_IMG_DATA_FORMAT_32_AS_32_32_32_32 = 0x3f,
};

enum GnmImgNumFormat {
    GNM_IMG_NUM_FORMAT_UNORM = 0x0,
    GNM_IMG_NUM_FORMAT_SNORM = 0x1,
    GNM_IMG_NUM_FORMAT_USCALED = 0x2,
    GNM_IMG_NUM_FORMAT_SSCALED = 0x3,
    GNM_IMG_NUM_FORMAT_UINT = 0x4,
    GNM_IMG_NUM_FORMAT_SINT = 0x5,
    GNM_IMG_NUM_FORMAT_SNORM_OGL = 0x6,
    GNM_IMG_NUM_FORMAT_FLOAT = 0x7,
    GNM_IMG_NUM_FORMAT_SRGB = 0x9,
    GNM_IMG_NUM_FORMAT_UBNORM = 0xa,
    GNM_IMG_NUM_FORMAT_UBNORM_OGL = 0xb,
    GNM_IMG_NUM_FORMAT_UBINT = 0xc,
    GNM_IMG_NUM_FORMAT_UBSCALED = 0xd,
};

enum GnmZFormat {
    GNM_Z_INVALID = 0x0,
    GNM_Z_16 = 0x1,
    GNM_Z_24 = 0x2,
    GNM_Z_32_FLOAT = 0x3,
};

enum GnmStencilFormat {
    GNM_STENCIL_INVALID = 0x0,
    GNM_STENCIL_8 = 0x1,
};

enum GnmChannel {
    GNM_CHAN_CONSTANT0 = 0x0,
    GNM_CHAN_CONSTANT1 = 0x1,
    GNM_CHAN_X = 0x4,
    GNM_CHAN_Y = 0x5,
    GNM_CHAN_Z = 0x6,
    GNM_CHAN_W = 0x7,
};

enum GnmSurfaceSwap {
    GNM_SWAP_STD = 0x0,
    GNM_SWAP_ALT = 0x1,
    GNM_SWAP_STD_REV = 0x2,
    GNM_SWAP_ALT_REV = 0x3,
};

union GnmDataFormat {
    struct {
        GnmImageFormat surfacefmt : 8;
        GnmImgNumFormat chantype : 4;
        GnmChannel chanx : 3;
        GnmChannel chany : 3;
        GnmChannel chanz : 3;
        GnmChannel chanw : 3;
        uint32_t _unused : 8;
    };
    uint32_t asuint;
};
static_assert(sizeof(GnmDataFormat) == 0x4, "");

GnmDataFormat gnmDfInitFromFmask(uint32_t numsamples, uint32_t numfrags);
GnmDataFormat gnmDfInitFromZ(GnmZFormat zfmt);

static inline GnmDataFormat gnmDfInitFromStencil(GnmStencilFormat stencilfmt,
                                                 GnmImgNumFormat chantype) {
    GnmDataFormat res = {
        .surfacefmt =
            stencilfmt == GNM_STENCIL_8 ? GNM_IMG_DATA_FORMAT_8 : GNM_IMG_DATA_FORMAT_INVALID,
        .chantype = chantype,
        .chanx = GNM_CHAN_X,
        .chany = GNM_CHAN_X,
        .chanz = GNM_CHAN_X,
        .chanw = GNM_CHAN_X,
    };
    return res;
}

static inline uint32_t gnmDfGetTexelsPerElement(const GnmDataFormat datafmt) {
    switch (datafmt.surfacefmt) {
    case GNM_IMG_DATA_FORMAT_BC1:
    case GNM_IMG_DATA_FORMAT_BC2:
    case GNM_IMG_DATA_FORMAT_BC3:
    case GNM_IMG_DATA_FORMAT_BC4:
    case GNM_IMG_DATA_FORMAT_BC5:
    case GNM_IMG_DATA_FORMAT_BC6:
    case GNM_IMG_DATA_FORMAT_BC7:
        return 16;
    case GNM_IMG_DATA_FORMAT_1:
    case GNM_IMG_DATA_FORMAT_1_REVERSED:
        return 8;
    default:
        return 1;
    }
}

uint32_t gnmDfGetNumComponents(const GnmDataFormat datafmt);
uint32_t gnmDfGetBitsPerElement(const GnmDataFormat datafmt);
static inline uint32_t gnmDfGetTotalBitsPerElement(const GnmDataFormat fmt) {
    const uint32_t bitsperelem = gnmDfGetBitsPerElement(fmt);
    const uint32_t texelsperelem = gnmDfGetTexelsPerElement(fmt);
    return bitsperelem * texelsperelem;
}
static inline uint32_t gnmDfGetBytesPerElement(const GnmDataFormat datafmt) {
    return gnmDfGetBitsPerElement(datafmt) / 8;
}
static inline uint32_t gnmDfGetTotalBytesPerElement(const GnmDataFormat fmt) {
    return gnmDfGetTotalBitsPerElement(fmt) / 8;
}
static inline bool gnmDfIsBlockCompressed(const GnmDataFormat datafmt) {
    switch (datafmt.surfacefmt) {
    case GNM_IMG_DATA_FORMAT_BC1:
    case GNM_IMG_DATA_FORMAT_BC2:
    case GNM_IMG_DATA_FORMAT_BC3:
    case GNM_IMG_DATA_FORMAT_BC4:
    case GNM_IMG_DATA_FORMAT_BC5:
    case GNM_IMG_DATA_FORMAT_BC6:
    case GNM_IMG_DATA_FORMAT_BC7:
        return true;
    default:
        return false;
    }
}

bool gnmDfGetRtChannelType(const GnmDataFormat datafmt, GnmSurfaceNumber* out);
bool gnmDfGetRtChannelOrder(const GnmDataFormat datafmt, GnmSurfaceSwap* out);

GnmZFormat gnmDfGetZFormat(const GnmDataFormat datafmt);
GnmStencilFormat gnmDfGetStencilFormat(const GnmDataFormat datafmt);

static inline uint32_t gnmDfGetTexelsPerElementWide(const GnmDataFormat fmt) {
    switch (fmt.surfacefmt) {
    case GNM_IMG_DATA_FORMAT_BC1:
    case GNM_IMG_DATA_FORMAT_BC2:
    case GNM_IMG_DATA_FORMAT_BC3:
    case GNM_IMG_DATA_FORMAT_BC4:
    case GNM_IMG_DATA_FORMAT_BC5:
    case GNM_IMG_DATA_FORMAT_BC6:
    case GNM_IMG_DATA_FORMAT_BC7:
        return 4;
    case GNM_IMG_DATA_FORMAT_1:
    case GNM_IMG_DATA_FORMAT_1_REVERSED:
        return 8;
    case GNM_IMG_DATA_FORMAT_GB_GR:
    case GNM_IMG_DATA_FORMAT_BG_RG:
        return 2;
    default:
        return 1;
    }
}
static inline uint32_t gnmDfGetTexelsPerElementTall(const GnmDataFormat fmt) {
    switch (fmt.surfacefmt) {
    case GNM_IMG_DATA_FORMAT_BC1:
    case GNM_IMG_DATA_FORMAT_BC2:
    case GNM_IMG_DATA_FORMAT_BC3:
    case GNM_IMG_DATA_FORMAT_BC4:
    case GNM_IMG_DATA_FORMAT_BC5:
    case GNM_IMG_DATA_FORMAT_BC6:
    case GNM_IMG_DATA_FORMAT_BC7:
        return 4;
    default:
        return 1;
    }
}

static const GnmDataFormat GNM_FMT_INVALID = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_INVALID,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_CONSTANT0,
    .chany = GNM_CHAN_CONSTANT0,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_CONSTANT0,
};
static const GnmDataFormat GNM_FMT_R8_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_CONSTANT0,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_A8_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_CONSTANT0,
    .chany = GNM_CHAN_CONSTANT0,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_X,
};
static const GnmDataFormat GNM_FMT_R8G8B8A8_SRGB = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8_8_8_8,
    .chantype = GNM_IMG_NUM_FORMAT_SRGB,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R8G8B8A8_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8_8_8_8,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R8G8B8A8_UINT = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8_8_8_8,
    .chantype = GNM_IMG_NUM_FORMAT_UINT,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_B8G8R8A8_SRGB = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8_8_8_8,
    .chantype = GNM_IMG_NUM_FORMAT_SRGB,
    .chanx = GNM_CHAN_Z,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_X,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_B8G8R8A8_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_8_8_8_8,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_Z,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_X,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R16_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_16,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_CONSTANT0,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_R16G16B16A16_SRGB = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_16_16_16_16,
    .chantype = GNM_IMG_NUM_FORMAT_SRGB,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R16G16B16A16_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_16_16_16_16,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R32_FLOAT = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32,
    .chantype = GNM_IMG_NUM_FORMAT_FLOAT,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_CONSTANT0,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_R32G32_FLOAT = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_FLOAT,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_CONSTANT0,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_R32G32B32_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_CONSTANT0,
};
static const GnmDataFormat GNM_FMT_R32G32B32_FLOAT = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_FLOAT,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_R32G32B32A32_SRGB = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_SRGB,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R32G32B32A32_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_R32G32B32A32_FLOAT = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_32_32_32_32,
    .chantype = GNM_IMG_NUM_FORMAT_FLOAT,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_BC6_SNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_BC6,
    .chantype = GNM_IMG_NUM_FORMAT_SNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_BC6_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_BC6,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_CONSTANT1,
};
static const GnmDataFormat GNM_FMT_BC7_UNORM = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_BC7,
    .chantype = GNM_IMG_NUM_FORMAT_UNORM,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
static const GnmDataFormat GNM_FMT_BC7_SRGB = {
    .surfacefmt = GNM_IMG_DATA_FORMAT_BC7,
    .chantype = GNM_IMG_NUM_FORMAT_SRGB,
    .chanx = GNM_CHAN_X,
    .chany = GNM_CHAN_Y,
    .chanz = GNM_CHAN_Z,
    .chanw = GNM_CHAN_W,
};
