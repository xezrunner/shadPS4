// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdbool>
#include "common/types.h"
#include "video_core/amdgpu/gpuaddr/dataformat.h"

enum GnmSampleSplit {
    GNM_ADDR_SAMPLE_SPLIT_1 = 0x0,
    GNM_ADDR_SAMPLE_SPLIT_2 = 0x1,
    GNM_ADDR_SAMPLE_SPLIT_4 = 0x2,
    GNM_ADDR_SAMPLE_SPLIT_8 = 0x3,
};

enum GnmMicroTileMode {
    GNM_SURF_DISPLAY_MICRO_TILING = 0x0,
    GNM_SURF_THIN_MICRO_TILING = 0x1,
    GNM_SURF_DEPTH_MICRO_TILING = 0x2,
    GNM_SURF_ROTATED_MICRO_TILING = 0x3,
    GNM_SURF_THICK_MICRO_TILING = 0x4,
};

enum GnmMacroTileMode {
    GNM_MACROTILEMODE_1x4_16 = 0x0,
    GNM_MACROTILEMODE_1x2_16 = 0x1,
    GNM_MACROTILEMODE_1x1_16 = 0x2,
    GNM_MACROTILEMODE_1x1_16_DUP = 0x3,
    GNM_MACROTILEMODE_1x1_8 = 0x4,
    GNM_MACROTILEMODE_1x1_4 = 0x5,
    GNM_MACROTILEMODE_1x1_2 = 0x6,
    GNM_MACROTILEMODE_1x1_2_DUP = 0x7,
    GNM_MACROTILEMODE_1x8_16 = 0x8,
    GNM_MACROTILEMODE_1x4_16_DUP = 0x9,
    GNM_MACROTILEMODE_1x2_16_DUP = 0xa,
    GNM_MACROTILEMODE_1x1_16_DUP2 = 0xb,
    GNM_MACROTILEMODE_1x1_8_DUP = 0xc,
    GNM_MACROTILEMODE_1x1_4_DUP = 0xd,
    GNM_MACROTILEMODE_1x1_2_DUP2 = 0xe,
    GNM_MACROTILEMODE_1x1_2_DUP3 = 0xf,
};

enum GnmTileMode {
    GNM_TM_DEPTH_2D_THIN_64 = 0x0,
    GNM_TM_DEPTH_2D_THIN_128 = 0x1,
    GNM_TM_DEPTH_2D_THIN_256 = 0x2,
    GNM_TM_DEPTH_2D_THIN_512 = 0x3,
    GNM_TM_DEPTH_2D_THIN_1K = 0x4,
    GNM_TM_DEPTH_1D_THIN = 0x5,
    GNM_TM_DEPTH_2D_THIN_PRT_256 = 0x6,
    GNM_TM_DEPTH_2D_THIN_PRT_1K = 0x7,

    GNM_TM_DISPLAY_LINEAR_ALIGNED = 0x8,
    GNM_TM_DISPLAY_1D_THIN = 0x9,
    GNM_TM_DISPLAY_2D_THIN = 0xa,
    GNM_TM_DISPLAY_THIN_PRT = 0xb,
    GNM_TM_DISPLAY_2D_THIN_PRT = 0xc,

    GNM_TM_THIN_1D_THIN = 0xd,
    GNM_TM_THIN_2D_THIN = 0xe,
    GNM_TM_THIN_3D_THIN = 0xf,
    GNM_TM_THIN_THIN_PRT = 0x10,
    GNM_TM_THIN_2D_THIN_PRT = 0x11,
    GNM_TM_THIN_3D_THIN_PRT = 0x12,

    GNM_TM_THICK_1D_THICK = 0x13,
    GNM_TM_THICK_2D_THICK = 0x14,
    GNM_TM_THICK_3D_THICK = 0x15,
    GNM_TM_THICK_THICK_PRT = 0x16,
    GNM_TM_THICK_2D_THICK_PRT = 0x17,
    GNM_TM_THICK_3D_THICK_PRT = 0x18,
    GNM_TM_THICK_2D_XTHICK = 0x19,
    GNM_TM_THICK_3D_XTHICK = 0x1a,

    GNM_TM_DISPLAY_LINEAR_GENERAL = 0x1f,
};

enum GnmArrayMode {
    GNM_ARRAY_LINEAR_GENERAL = 0x0,
    GNM_ARRAY_LINEAR_ALIGNED = 0x1,
    GNM_ARRAY_1D_TILED_THIN1 = 0x2,
    GNM_ARRAY_1D_TILED_THICK = 0x3,
    GNM_ARRAY_2D_TILED_THIN1 = 0x4,
    GNM_ARRAY_PRT_TILED_THIN1 = 0x5,
    GNM_ARRAY_PRT_2D_TILED_THIN1 = 0x6,
    GNM_ARRAY_2D_TILED_THICK = 0x7,
    GNM_ARRAY_2D_TILED_XTHICK = 0x8,
    GNM_ARRAY_PRT_TILED_THICK = 0x9,
    GNM_ARRAY_PRT_2D_TILED_THICK = 0xa,
    GNM_ARRAY_PRT_3D_TILED_THIN1 = 0xb,
    GNM_ARRAY_3D_TILED_THIN1 = 0xc,
    GNM_ARRAY_3D_TILED_THICK = 0xd,
    GNM_ARRAY_3D_TILED_XTHICK = 0xe,
    GNM_ARRAY_PRT_3D_TILED_THICK = 0xf,
};

enum GnmNumBanks {
    GNM_SURF_2_BANK = 0x0,
    GNM_SURF_4_BANK = 0x1,
    GNM_SURF_8_BANK = 0x2,
    GNM_SURF_16_BANK = 0x3,
};

enum GnmGpuMode {
    GNM_GPU_BASE = 0x0,
    GNM_GPU_NEO = 0x1,
};

enum GnmBankWidth {
    GNM_SURF_BANK_WIDTH_1 = 0x0,
    GNM_SURF_BANK_WIDTH_2 = 0x1,
    GNM_SURF_BANK_WIDTH_4 = 0x2,
    GNM_SURF_BANK_WIDTH_8 = 0x3,
};

enum GnmBankHeight {
    GNM_SURF_BANK_HEIGHT_1 = 0x0,
    GNM_SURF_BANK_HEIGHT_2 = 0x1,
    GNM_SURF_BANK_HEIGHT_4 = 0x2,
    GNM_SURF_BANK_HEIGHT_8 = 0x3,
};

enum GnmPipeConfig {
    GNM_ADDR_SURF_P2 = 0x0,
    GNM_ADDR_SURF_P4_8x16 = 0x4,
    GNM_ADDR_SURF_P4_16x16 = 0x5,
    GNM_ADDR_SURF_P4_16x32 = 0x6,
    GNM_ADDR_SURF_P4_32x32 = 0x7,
    GNM_ADDR_SURF_P8_16x16_8x16 = 0x8,
    GNM_ADDR_SURF_P8_16x32_8x16 = 0x9,
    GNM_ADDR_SURF_P8_32x32_8x16 = 0xa,
    GNM_ADDR_SURF_P8_16x32_16x16 = 0xb,
    GNM_ADDR_SURF_P8_32x32_16x16 = 0xc,
    GNM_ADDR_SURF_P8_32x32_16x32 = 0xd,
    GNM_ADDR_SURF_P8_32x64_32x32 = 0xe,
    GNM_ADDR_SURF_P16_32x32_8x16 = 0x10,
    GNM_ADDR_SURF_P16_32x32_16x16 = 0x11,
};

enum GnmMacroTileAspect {
    GNM_SURF_MACRO_ASPECT_1 = 0x0,
    GNM_SURF_MACRO_ASPECT_2 = 0x1,
    GNM_SURF_MACRO_ASPECT_4 = 0x2,
    GNM_SURF_MACRO_ASPECT_8 = 0x3,
};

enum GnmTileSplit {
    GNM_SURF_TILE_SPLIT_64B = 0x0,
    GNM_SURF_TILE_SPLIT_128B = 0x1,
    GNM_SURF_TILE_SPLIT_256B = 0x2,
    GNM_SURF_TILE_SPLIT_512B = 0x3,
    GNM_SURF_TILE_SPLIT_1KB = 0x4,
    GNM_SURF_TILE_SPLIT_2KB = 0x5,
    GNM_SURF_TILE_SPLIT_4KB = 0x6,
};

enum GpaSurfaceType {
    GPA_SURFACE_COLORDISPLAY,
    GPA_SURFACE_COLOR,
    GPA_SURFACE_DEPTHSTENCIL,
    GPA_SURFACE_DEPTH,
    GPA_SURFACE_STENCIL,
    GPA_SURFACE_FMASK,
    GPA_SURFACE_TEXTUREFLAT,
    GPA_SURFACE_TEXTUREVOLUME,
    GPA_SURFACE_TEXTURECUBEMAP,
    GPA_SURFACE_RWTEXTUREFLAT,
    GPA_SURFACE_RWTEXTUREVOLUME,
    GPA_SURFACE_RWTEXTURECUBEMAP,
};

struct GpaSurfaceFlags {
    u32 colortarget : 1;
    u32 depthtarget : 1;
    u32 stenciltarget : 1;
    u32 texture : 1;
    u32 cube : 1;
    u32 volume : 1;
    u32 fmask : 1;
    u32 cubeasarray : 1;
    u32 overlay : 1;
    u32 display : 1;
    u32 prt : 1;
    u32 pow2pad : 1;
    u32 texcompatible : 1;
    u32 _unused : 19;
};
static_assert(sizeof(GpaSurfaceFlags) == 0x4, "");

struct GpaSurfaceProperties {
    GnmTileMode tilemode;
    GpaSurfaceFlags flags;
};

struct GpaHtileParams {
    u32 pitch;
    u32 height;
    u32 numslices;
    u32 numfrags;
    u32 bpp;

    GnmArrayMode arraymode;
    GnmNumBanks banks;
    GnmPipeConfig pipeconfig;

    GnmGpuMode mingpumode;

    struct {
        u32 tccompatible : 1;
        u32 reserved : 31;
    } flags;
};

struct GpaCmaskParams {
    u32 pitch;
    u32 height;
    u32 numslices;
    u32 numfrags;
    u32 bpp;
    GnmTileMode tilemode;

    GnmGpuMode mingpumode;

    struct {
        u32 tccompatible : 1;
        u32 reserved : 31;
    } flags;
};

struct GpaFmaskParams {
    u32 pitch;
    u32 height;
    u32 numslices;
    u32 numfrags;
    u32 bpp;
    GnmTileMode tilemode;

    GnmGpuMode mingpumode;

    bool isblockcompressed;
};

struct GpaTileInfo {
    GnmArrayMode arraymode;
    GnmNumBanks banks;
    GnmBankWidth bankwidth;
    GnmBankHeight bankheight;
    GnmMacroTileAspect macroaspectratio;
    GnmTileSplit tilesplit;
    GnmPipeConfig pipeconfig;
};

struct GpaSurfaceInfo {
    u32 pitch;
    u32 height;
    u32 depth;
    uint64_t surfacesize;
    u32 basealign;
    u32 pitchalign;
    u32 heightalign;
    u32 depthalign;
    u32 bitsperelem;

    u32 blockwidth;
    u32 blockheight;

    GnmTileMode tilemode;
    GpaTileInfo tileinfo;

    struct {
        u32 istexcompatible : 1;
        u32 _unused : 31;
    };
};

struct GpaHtileInfo {
    u32 pitch;
    u32 height;
    u32 basealign;
    u32 bpp;
    u32 macrowidth;
    u32 macroheight;
    uint64_t htilebytes;
    uint64_t slicebytes;
};

struct GpaCmaskInfo {
    u32 pitch;
    u32 height;
    u32 basealign;
    u32 bpp;
    u32 macrowidth;
    u32 macroheight;
    u32 blockmax;
    uint64_t cmaskbytes;
    uint64_t slicebytes;
};

struct GpaFmaskInfo {
    u32 pitch;
    u32 height;
    u32 basealign;
    u32 pitchalign;
    u32 heightalign;
    u32 bpp;
    uint64_t fmaskbytes;
    uint64_t slicebytes;
};

struct GpaSurfaceIndex {
    u32 arrayindex;
    u32 face;
    u32 mip;
    u32 depth;
    u32 fragment;
    u32 sample;
};

struct GpaTilingParams {
    GnmTileMode tilemode;
    GnmGpuMode mingpumode;

    u32 linearwidth;
    u32 linearheight;
    u32 lineardepth;
    u32 numfragsperpixel;
    u32 basetiledpitch;

    u32 miplevel;
    u32 arrayslice;
    GpaSurfaceFlags surfaceflags;
    u32 bitsperfrag;
    bool isblockcompressed;
};

struct GpaSurfaceRegion {
    u32 left;  // -X
    u32 top;   // -Y
    u32 front; // -Z

    u32 right;  // +X
    u32 bottom; // +Y
    u32 back;   // +Z
};

enum GnmTextureType {
    GNM_TEXTURE_1D = 0x8,
    GNM_TEXTURE_2D = 0x9,
    GNM_TEXTURE_3D = 0xa,
    GNM_TEXTURE_CUBEMAP = 0xb,
    GNM_TEXTURE_1D_ARRAY = 0xc,
    GNM_TEXTURE_2D_ARRAY = 0xd,
    GNM_TEXTURE_2D_MSAA = 0xe,
    GNM_TEXTURE_2D_ARRAY_MSAA = 0xf,
};

struct GpaTextureInfo {
    GnmTextureType type;
    GnmDataFormat fmt;

    u32 width;
    u32 height;
    u32 pitch;
    u32 depth;

    u32 numfrags;
    u32 nummips;
    u32 numslices;

    GnmTileMode tm;
    GnmGpuMode mingpumode;

    bool pow2pad;
};
