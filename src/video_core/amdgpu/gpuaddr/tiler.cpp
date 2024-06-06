// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstring>
#include "video_core/amdgpu/gpuaddr/gpuaddr_private.h"

typedef struct {
    GnmGpuMode mingpumode;
    GnmTileMode tilemode;
    GpaTileInfo tileinfo;

    uint32_t linearwidth;
    uint32_t linearheight;
    uint32_t lineardepth;
    uint32_t paddedwidth;
    uint32_t paddedheight;
    uint32_t paddeddepth;

    uint32_t bitsperelement;
    uint32_t numfragsperpixel;

    uint32_t bankswizzlemask;
    uint32_t pipeswizzlemask;
} GpaTilerContext;

static GpaError createtilerctx(GpaTilerContext* ctx, size_t outsurfsize, size_t insurfsize,
                               const GpaTilingParams* tp) {
    GpaSurfaceInfo surfinfo = {0};
    GpaError err = gpaComputeSurfaceInfo(&surfinfo, tp);
    if (err != GPA_ERR_OK) {
        return err;
    }

    // check if buffers are large enough
    if (surfinfo.surfacesize > insurfsize || surfinfo.surfacesize > outsurfsize) {
        return GPA_ERR_OVERFLOW;
    }

    GpaTileInfo tileinfo = {};
    err = gpaGetTileInfo(&tileinfo, tp->tilemode, tp->bitsperfrag, tp->numfragsperpixel,
                         tp->mingpumode);
    if (err != GPA_ERR_OK) {
        return err;
    }

    *ctx = (GpaTilerContext){
        .mingpumode = tp->mingpumode,
        .tilemode = tp->tilemode,
        .tileinfo = tileinfo,

        .linearwidth = tp->linearwidth,
        .linearheight = tp->linearheight,
        .lineardepth = tp->lineardepth,
        .paddedwidth = surfinfo.pitch,
        .paddedheight = surfinfo.height,
        .paddeddepth = surfinfo.depth,

        .bitsperelement = tp->bitsperfrag,
        .numfragsperpixel = tp->numfragsperpixel,

        // TODO: calc swizzle?
        .bankswizzlemask = 0,
        .pipeswizzlemask = 0,
    };

    // TODO: why is this here?
    // BC7 tests fail is this isn't here,
    // but shouldn't this be handled by something else already?
    if (!gpaIsLinear(surfinfo.tileinfo.arraymode) && tp->isblockcompressed) {
        switch (tp->bitsperfrag) {
        case 1:
            ctx->bitsperelement *= 8;
            ctx->linearwidth = std::max((ctx->linearwidth + 7) / 8, 1U);
            ctx->paddedwidth = std::max((ctx->paddedwidth + 7) / 8, 1U);
            break;
        case 4:
        case 8:
            ctx->bitsperelement *= 16;
            ctx->linearwidth = std::max((ctx->linearwidth + 3) / 4, 1U);
            ctx->linearheight = std::max((ctx->linearheight + 3) / 4, 1U);
            ctx->paddedwidth = std::max((ctx->paddedwidth + 3) / 4, 1U);
            ctx->paddedheight = std::max((ctx->paddedheight + 3) / 4, 1U);
            break;
        case 16:
        default:
            return GPA_ERR_UNSUPPORTED;
        }
    }

    return GPA_ERR_OK;
}

static GpaError ComputePixelIndexWithinMicroTile(
    uint32_t* outIndex,
    uint32_t x,                    ///< [in] x coord
    uint32_t y,                    ///< [in] y coord
    uint32_t z,                    ///< [in] slice/depth index
    uint32_t bpp,                  ///< [in] bits per pixel
    GnmArrayMode arrayMode,        ///< [in] tile mode
    GnmMicroTileMode microTileType ///< [in] pixel order in display/non-display mode
) {
    uint32_t pixelBit0 = 0;
    uint32_t pixelBit1 = 0;
    uint32_t pixelBit2 = 0;
    uint32_t pixelBit3 = 0;
    uint32_t pixelBit4 = 0;
    uint32_t pixelBit5 = 0;
    uint32_t pixelBit6 = 0;
    uint32_t pixelBit7 = 0;
    uint32_t pixelBit8 = 0;

    const uint32_t x0 = (x >> 0) & 1;
    const uint32_t x1 = (x >> 1) & 1;
    const uint32_t x2 = (x >> 2) & 1;
    const uint32_t y0 = (y >> 0) & 1;
    const uint32_t y1 = (y >> 1) & 1;
    const uint32_t y2 = (y >> 2) & 1;
    const uint32_t z0 = (z >> 0) & 1;
    const uint32_t z1 = (z >> 1) & 1;
    const uint32_t z2 = (z >> 2) & 1;

    const uint32_t thickness = gpaGetMicroTileThickness(arrayMode);

    // Compute the pixel number within the micro tile.

    if (microTileType != GNM_SURF_THICK_MICRO_TILING) {
        if (microTileType == GNM_SURF_DISPLAY_MICRO_TILING) {
            switch (bpp) {
            case 8:
                pixelBit0 = x0;
                pixelBit1 = x1;
                pixelBit2 = x2;
                pixelBit3 = y1;
                pixelBit4 = y0;
                pixelBit5 = y2;
                break;
            case 16:
                pixelBit0 = x0;
                pixelBit1 = x1;
                pixelBit2 = x2;
                pixelBit3 = y0;
                pixelBit4 = y1;
                pixelBit5 = y2;
                break;
            case 32:
                pixelBit0 = x0;
                pixelBit1 = x1;
                pixelBit2 = y0;
                pixelBit3 = x2;
                pixelBit4 = y1;
                pixelBit5 = y2;
                break;
            case 64:
                pixelBit0 = x0;
                pixelBit1 = y0;
                pixelBit2 = x1;
                pixelBit3 = x2;
                pixelBit4 = y1;
                pixelBit5 = y2;
                break;
            case 128:
                pixelBit0 = y0;
                pixelBit1 = x0;
                pixelBit2 = x1;
                pixelBit3 = x2;
                pixelBit4 = y1;
                pixelBit5 = y2;
                break;
            default:
                return GPA_ERR_INTERNAL_ERROR;
            }
        } else if (microTileType != GNM_SURF_DISPLAY_MICRO_TILING ||
                   microTileType == GNM_SURF_DEPTH_MICRO_TILING) {
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = x1;
            pixelBit3 = y1;
            pixelBit4 = x2;
            pixelBit5 = y2;
        } else if (microTileType == GNM_SURF_ROTATED_MICRO_TILING) {
            if (thickness != 1) {
                return GPA_ERR_INTERNAL_ERROR;
            }

            switch (bpp) {
            case 8:
                pixelBit0 = y0;
                pixelBit1 = y1;
                pixelBit2 = y2;
                pixelBit3 = x1;
                pixelBit4 = x0;
                pixelBit5 = x2;
                break;
            case 16:
                pixelBit0 = y0;
                pixelBit1 = y1;
                pixelBit2 = y2;
                pixelBit3 = x0;
                pixelBit4 = x1;
                pixelBit5 = x2;
                break;
            case 32:
                pixelBit0 = y0;
                pixelBit1 = y1;
                pixelBit2 = x0;
                pixelBit3 = y2;
                pixelBit4 = x1;
                pixelBit5 = x2;
                break;
            case 64:
                pixelBit0 = y0;
                pixelBit1 = x0;
                pixelBit2 = y1;
                pixelBit3 = x1;
                pixelBit4 = x2;
                pixelBit5 = y2;
                break;
            default:
                return GPA_ERR_INTERNAL_ERROR;
            }
        }

        if (thickness > 1) {
            pixelBit6 = z0;
            pixelBit7 = z1;
        }
    } else // ADDR_THICK
    {
        if (thickness <= 1) {
            return GPA_ERR_INTERNAL_ERROR;
        }

        switch (bpp) {
        case 8:
        case 16:
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = x1;
            pixelBit3 = y1;
            pixelBit4 = z0;
            pixelBit5 = z1;
            break;
        case 32:
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = x1;
            pixelBit3 = z0;
            pixelBit4 = y1;
            pixelBit5 = z1;
            break;
        case 64:
        case 128:
            pixelBit0 = x0;
            pixelBit1 = y0;
            pixelBit2 = z0;
            pixelBit3 = x1;
            pixelBit4 = y1;
            pixelBit5 = z1;
            break;
        default:
            return GPA_ERR_INTERNAL_ERROR;
        }

        pixelBit6 = x2;
        pixelBit7 = y2;
    }

    if (thickness == 8) {
        pixelBit8 = z2;
    }

    *outIndex =
        ((pixelBit0) | (pixelBit1 << 1) | (pixelBit2 << 2) | (pixelBit3 << 3) | (pixelBit4 << 4) |
         (pixelBit5 << 5) | (pixelBit6 << 6) | (pixelBit7 << 7) | (pixelBit8 << 8));
    return GPA_ERR_OK;
}

static uint64_t ComputeSurfaceAddrFromCoordLinear(
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

static GpaError ComputeSurfaceAddrFromCoordMicroTiled(
    uint32_t x,                     ///< [in] x coordinate
    uint32_t y,                     ///< [in] y coordinate
    uint32_t slice,                 ///< [in] slice index
    uint32_t sample,                ///< [in] sample index
    uint32_t bpp,                   ///< [in] bits per pixel
    uint32_t pitch,                 ///< [in] pitch, in pixels
    uint32_t height,                ///< [in] height, in pixels
    uint32_t numSamples,            ///< [in] number of samples
    GnmArrayMode arrayMode,         ///< [in] tile mode
    GnmMicroTileMode microTileType, ///< [in] micro tiling type
    bool isDepthSampleOrder,        ///< [in] TRUE if depth sample ordering is used
    uint64_t* outAddr,              ///< [out] byte position
    uint32_t* pBitPosition          ///< [out] bit position, e.g. FMT_1 will use this
) {
    uint32_t microTileBytes;
    uint64_t sliceBytes;
    uint32_t microTilesPerRow;
    uint32_t microTileIndexX;
    uint32_t microTileIndexY;
    uint32_t microTileIndexZ;
    uint64_t sliceOffset;
    uint64_t microTileOffset;
    uint32_t sampleOffset;
    uint32_t pixelIndex;
    uint32_t pixelOffset;

    const uint32_t microTileThickness = gpaGetMicroTileThickness(arrayMode);

    //
    // Compute the micro tile size.
    //
    microTileBytes = BitsToBytes32(MicroTilePixels * microTileThickness * bpp * numSamples);

    //
    // Compute the slice size.
    //
    sliceBytes = BitsToBytes64((uint64_t)pitch * height * microTileThickness * bpp * numSamples);

    //
    // Compute the number of micro tiles per row.
    //
    microTilesPerRow = pitch / MicroTileWidth;

    //
    // Compute the micro tile index.
    //
    microTileIndexX = x / MicroTileWidth;
    microTileIndexY = y / MicroTileHeight;
    microTileIndexZ = slice / microTileThickness;

    //
    // Compute the slice offset.
    //
    sliceOffset = (uint64_t)microTileIndexZ * sliceBytes;

    //
    // Compute the offset to the micro tile containing the specified
    // coordinate.
    //
    microTileOffset =
        ((uint64_t)microTileIndexY * microTilesPerRow + microTileIndexX) * microTileBytes;

    //
    // Compute the pixel index within the micro tile.
    //
    GpaError err =
        ComputePixelIndexWithinMicroTile(&pixelIndex, x, y, slice, bpp, arrayMode, microTileType);
    if (err != GPA_ERR_OK) {
        return err;
    }

    // Compute the sample offset.
    //
    if (isDepthSampleOrder) {
        //
        // For depth surfaces, samples are stored contiguously
        // for each element, so the sample offset is the sample
        // number times the element size.
        //
        sampleOffset = sample * bpp;
        pixelOffset = pixelIndex * bpp * numSamples;
    } else {
        //
        // For color surfaces, all elements for a particular
        // sample are stored contiguously, so the sample offset
        // is the sample number times the micro tile size
        // divided yBit the number of samples.
        //
        sampleOffset = sample * (microTileBytes * 8 / numSamples);
        pixelOffset = pixelIndex * bpp;
    }

    //
    // Compute the bit position of the pixel.  Each element is
    // stored with one bit per sample.
    //

    uint32_t elemOffset = sampleOffset + pixelOffset;

    *pBitPosition = elemOffset % 8;
    elemOffset /= 8;

    //
    // Combine the slice offset, micro tile offset, sample offset,
    // and pixel offsets.
    //
    *outAddr = sliceOffset + microTileOffset + elemOffset;

    return GPA_ERR_OK;
}

static GpaError ComputePipeFromCoord(uint32_t x,                   ///< [in] x coordinate
                                     uint32_t y,                   ///< [in] y coordinate
                                     uint32_t slice,               ///< [in] slice index
                                     GnmArrayMode arrayMode,       ///< [in] tile mode
                                     uint32_t pipeSwizzle,         ///< [in] pipe swizzle
                                     const GpaTileInfo* pTileInfo, ///< [in] Tile info
                                     uint32_t* outCoord            ///< [out] resulting coordinate
) {
    uint32_t pipe;
    uint32_t pipeBit0 = 0;
    uint32_t pipeBit1 = 0;
    uint32_t pipeBit2 = 0;
    uint32_t pipeBit3 = 0;
    uint32_t sliceRotation;
    uint32_t numPipes = 0;

    const uint32_t tx = x / MicroTileWidth;
    const uint32_t ty = y / MicroTileHeight;
    const uint32_t x3 = (tx >> 0) & 1;
    const uint32_t x4 = (tx >> 1) & 1;
    const uint32_t x5 = (tx >> 2) & 1;
    const uint32_t x6 = (tx >> 3) & 1;
    const uint32_t y3 = (ty >> 0) & 1;
    const uint32_t y4 = (ty >> 1) & 1;
    const uint32_t y5 = (ty >> 2) & 1;
    const uint32_t y6 = (ty >> 3) & 1;

    switch (pTileInfo->pipeconfig) {
    case GNM_ADDR_SURF_P2:
        pipeBit0 = x3 ^ y3;
        numPipes = 2;
        break;
    case GNM_ADDR_SURF_P8_32x32_8x16:
        pipeBit0 = x4 ^ y3 ^ x5;
        pipeBit1 = x3 ^ y4;
        pipeBit2 = x5 ^ y5;
        numPipes = 8;
        break;
    case GNM_ADDR_SURF_P8_32x32_16x16:
        pipeBit0 = x3 ^ y3 ^ x4;
        pipeBit1 = x4 ^ y4;
        pipeBit2 = x5 ^ y5;
        numPipes = 8;
        break;
    case GNM_ADDR_SURF_P16_32x32_8x16:
        pipeBit0 = x4 ^ y3;
        pipeBit1 = x3 ^ y4;
        pipeBit2 = x5 ^ y6;
        pipeBit3 = x6 ^ y5;
        numPipes = 16;
        break;
    default:
        return GPA_ERR_UNSUPPORTED;
    }

    pipe = pipeBit0 | (pipeBit1 << 1) | (pipeBit2 << 2) | (pipeBit3 << 3);

    const uint32_t microTileThickness = gpaGetMicroTileThickness(arrayMode);

    //
    // Apply pipe rotation for the slice.
    //
    switch (arrayMode) {
    case GNM_ARRAY_3D_TILED_THIN1: // fall through thin
    case GNM_ARRAY_3D_TILED_THICK: // fall through thick
    case GNM_ARRAY_3D_TILED_XTHICK:
        sliceRotation = std::max(1, (int32_t)(numPipes / 2) - 1) * (slice / microTileThickness);
        break;
    default:
        sliceRotation = 0;
        break;
    }
    pipeSwizzle += sliceRotation;
    pipeSwizzle &= (numPipes - 1);

    *outCoord = pipe ^ pipeSwizzle;
    return GPA_ERR_OK;
}

static GpaError ComputeBankFromCoord(
    uint32_t x,                   ///< [in] x coordinate
    uint32_t y,                   ///< [in] y coordinate
    uint32_t slice,               ///< [in] slice index
    GnmArrayMode arrayMode,       ///< [in] tile mode
    uint32_t bankSwizzle,         ///< [in] bank swizzle
    uint32_t tileSplitSlice,      ///< [in] If the size of the pixel offset is
                                  ///< larger than the
                                  ///  tile split size, then the pixel will be
                                  ///  moved to a separate slice. This value
                                  ///  equals pixelOffset / tileSplitBytes in
                                  ///  this case. Otherwise this is 0.
    const GpaTileInfo* pTileInfo, ///< [in] tile info
    uint32_t* outCoord            ///< [out] resulting coord
) {
    const uint32_t pipes = gpaGetPipeCount(pTileInfo->pipeconfig);
    uint32_t bankBit0 = 0;
    uint32_t bankBit1 = 0;
    uint32_t bankBit2 = 0;
    uint32_t bankBit3 = 0;
    uint32_t sliceRotation;
    uint32_t tileSplitRotation;
    uint32_t bank;
    const uint32_t numBanks = 2 << pTileInfo->banks;
    const uint32_t bankWidth = (1 << pTileInfo->bankwidth);
    const uint32_t bankHeight = (1 << pTileInfo->bankheight);

    const uint32_t tx = x / MicroTileWidth / (bankWidth * pipes);
    const uint32_t ty = y / MicroTileHeight / bankHeight;

    const uint32_t x3 = (tx >> 0) & 1;
    const uint32_t x4 = (tx >> 1) & 1;
    const uint32_t x5 = (tx >> 2) & 1;
    const uint32_t x6 = (tx >> 3) & 1;
    const uint32_t y3 = (ty >> 0) & 1;
    const uint32_t y4 = (ty >> 1) & 1;
    const uint32_t y5 = (ty >> 2) & 1;
    const uint32_t y6 = (ty >> 3) & 1;

    switch (numBanks) {
    case 16:
        bankBit0 = x3 ^ y6;
        bankBit1 = x4 ^ y5 ^ y6;
        bankBit2 = x5 ^ y4;
        bankBit3 = x6 ^ y3;
        break;
    case 8:
        bankBit0 = x3 ^ y5;
        bankBit1 = x4 ^ y4 ^ y5;
        bankBit2 = x5 ^ y3;
        break;
    case 4:
        bankBit0 = x3 ^ y4;
        bankBit1 = x4 ^ y3;
        break;
    case 2:
        bankBit0 = x3 ^ y3;
        break;
    default:
        return GPA_ERR_UNSUPPORTED;
    }

    bank = bankBit0 | (bankBit1 << 1) | (bankBit2 << 2) | (bankBit3 << 3);

    // Bits2Number(4, bankBit3, bankBit2, bankBit1, bankBit0);

    // bank = HwlPreAdjustBank((x / MicroTileWidth), bank, pTileInfo);
    //
    //  Compute bank rotation for the slice.
    //
    const uint32_t microTileThickness = gpaGetMicroTileThickness(arrayMode);

    switch (arrayMode) {
    case GNM_ARRAY_2D_TILED_THIN1: // fall through
    case GNM_ARRAY_2D_TILED_THICK: // fall through
    case GNM_ARRAY_2D_TILED_XTHICK:
        sliceRotation = ((numBanks / 2) - 1) * (slice / microTileThickness);
        break;
    case GNM_ARRAY_3D_TILED_THIN1: // fall through
    case GNM_ARRAY_3D_TILED_THICK: // fall through
    case GNM_ARRAY_3D_TILED_XTHICK:
        sliceRotation = std::max(1u, (pipes / 2) - 1) * (slice / microTileThickness) / pipes;
        break;
    default:
        sliceRotation = 0;
        break;
    }

    //
    // Compute bank rotation for the tile split slice.
    //
    // The sample slice will be non-zero if samples must be split
    // across multiple slices. This situation arises when the micro
    // tile size multiplied yBit the number of samples exceeds the
    // split size (set in GB_ADDR_CONFIG).
    //
    switch (arrayMode) {
    case GNM_ARRAY_2D_TILED_THIN1:     // fall through
    case GNM_ARRAY_3D_TILED_THIN1:     // fall through
    case GNM_ARRAY_PRT_2D_TILED_THIN1: // fall through
    case GNM_ARRAY_PRT_3D_TILED_THIN1: // fall through
        tileSplitRotation = ((numBanks / 2) + 1) * tileSplitSlice;
        break;
    default:
        tileSplitRotation = 0;
        break;
    }

    //
    // Apply bank rotation for the slice and tile split slice.
    //
    bank ^= bankSwizzle + sliceRotation;
    bank ^= tileSplitRotation;

    bank &= (numBanks - 1);

    *outCoord = bank;
    return GPA_ERR_OK;
}

static GpaError ComputeSurfaceAddrFromCoordMacroTiled(
    uint32_t x,                     ///< [in] x coordinate
    uint32_t y,                     ///< [in] y coordinate
    uint32_t slice,                 ///< [in] slice index
    uint32_t sample,                ///< [in] sample index
    uint32_t bpp,                   ///< [in] bits per pixel
    uint32_t pitch,                 ///< [in] surface pitch, in pixels
    uint32_t height,                ///< [in] surface height, in pixels
    uint32_t numSamples,            ///< [in] number of samples
    GnmArrayMode arrayMode,         ///< [in] tile mode
    GnmMicroTileMode microTileType, ///< [in] micro tiling type
    bool isDepthSampleOrder,        ///< [in] TRUE if it depth sample ordering is used
    uint32_t pipeSwizzle,           ///< [in] pipe swizzle
    uint32_t bankSwizzle,           ///< [in] bank swizzle
    const GpaTileInfo* pTileInfo,   ///< [in] bank structure  **All fields to be
                                    ///< valid on entry**
    uint64_t* pBytePosition,        ///< [out] byte position
    uint32_t* pBitPosition          ///< [out] bit position, e.g. FMT_1 will use this
) {
    uint64_t addr;

    uint32_t microTileBytes;
    uint32_t microTileBits;
    uint32_t sampleOffset;
    uint32_t pixelIndex;
    uint32_t pixelOffset;
    uint32_t elementOffset;
    uint32_t tileSplitSlice;
    uint32_t pipe;
    uint32_t bank;
    uint64_t sliceBytes;
    uint64_t sliceOffset;
    uint32_t macroTilePitch;
    uint32_t macroTileHeight;
    uint32_t macroTilesPerRow;
    uint32_t macroTilesPerSlice;
    uint64_t macroTileBytes;
    uint32_t macroTileIndexX;
    uint32_t macroTileIndexY;
    uint64_t macroTileOffset;
    uint64_t totalOffset;
    uint64_t pipeInterleaveMask;
    uint64_t bankInterleaveMask;
    uint64_t pipeInterleaveOffset;
    uint32_t bankInterleaveOffset;
    uint64_t offset;
    uint32_t tileRowIndex;
    uint32_t tileColumnIndex;
    uint32_t tileIndex;
    uint32_t tileOffset;

    uint32_t microTileThickness = gpaGetMicroTileThickness(arrayMode);

    const uint32_t banks = 2 << pTileInfo->banks;
    const uint32_t bankWidth = (1 << pTileInfo->bankwidth);
    const uint32_t bankHeight = (1 << pTileInfo->bankheight);
    const uint32_t macroAspectRatio = (1 << pTileInfo->macroaspectratio);
    const uint32_t tileSplitBytes =
        GetTileSplitBytes(pTileInfo->tilesplit, bpp, microTileThickness);

    //
    // Compute the number of group, pipe, and bank bits.
    //
    uint32_t numPipes = gpaGetPipeCount(pTileInfo->pipeconfig);
    uint32_t numPipeInterleaveBits = log2(PIPE_INTERLEAVE_BYTES);
    uint32_t numPipeBits = log2(numPipes);
    uint32_t numBankInterleaveBits = log2(BANK_INTERLEAVE);
    uint32_t numBankBits = log2(banks);

    //
    // Compute the micro tile size.
    //
    microTileBits = MicroTilePixels * microTileThickness * bpp * numSamples;

    microTileBytes = microTileBits / 8;
    //
    // Compute the pixel index within the micro tile.
    //
    GpaError err =
        ComputePixelIndexWithinMicroTile(&pixelIndex, x, y, slice, bpp, arrayMode, microTileType);
    if (err != GPA_ERR_OK) {
        return err;
    }

    //
    // Compute the sample offset and pixel offset.
    //
    if (isDepthSampleOrder) {
        //
        // For depth surfaces, samples are stored contiguously
        // for each element, so the sample offset is the sample
        // number times the element size.
        //
        sampleOffset = sample * bpp;
        pixelOffset = pixelIndex * bpp * numSamples;
    } else {
        //
        // For color surfaces, all elements for a particular
        // sample are stored contiguously, so the sample offset
        // is the sample number times the micro tile size
        // divided yBit the number of samples.
        //
        sampleOffset = sample * (microTileBits / numSamples);
        pixelOffset = pixelIndex * bpp;
    }

    //
    // Compute the element offset.
    //
    elementOffset = pixelOffset + sampleOffset;

    *pBitPosition = (uint32_t)(elementOffset % 8);

    elementOffset /= 8; // bit-to-byte

    //
    // Determine if tiles need to be split across slices.
    //
    // If the size of the micro tile is larger than the tile split
    // size, then the tile will be split across multiple slices.
    //
    uint32_t slicesPerTile = 1;

    if ((microTileBytes > tileSplitBytes) &&
        (microTileThickness == 1)) { // don't support for thick mode

        //
        // Compute the number of slices per tile.
        //
        slicesPerTile = microTileBytes / tileSplitBytes;

        //
        // Compute the tile split slice number for use in
        // rotating the bank.
        //
        tileSplitSlice = elementOffset / tileSplitBytes;

        //
        // Adjust the element offset to account for the portion
        // of the tile that is being moved to a new slice..
        //
        elementOffset %= tileSplitBytes;

        //
        // Adjust the microTileBytes size to tileSplitBytes size
        // since a new slice..
        //
        microTileBytes = tileSplitBytes;
    } else {
        tileSplitSlice = 0;
    }

    //
    // Compute macro tile pitch and height.
    //
    macroTilePitch = (MicroTileWidth * bankWidth * numPipes) * macroAspectRatio;
    macroTileHeight = (MicroTileHeight * bankHeight * banks) / macroAspectRatio;

    //
    // Compute the number of bytes per macro tile. Note: bytes of
    // the same bank/pipe actually
    //
    macroTileBytes = (uint64_t)microTileBytes * (macroTilePitch / MicroTileWidth) *
                     (macroTileHeight / MicroTileHeight) / (numPipes * banks);

    //
    // Compute the number of macro tiles per row.
    //
    macroTilesPerRow = pitch / macroTilePitch;

    //
    // Compute the offset to the macro tile containing the specified
    // coordinate.
    //
    macroTileIndexX = x / macroTilePitch;
    macroTileIndexY = y / macroTileHeight;
    macroTileOffset = ((macroTileIndexY * macroTilesPerRow) + macroTileIndexX) * macroTileBytes;

    //
    // Compute the number of macro tiles per slice.
    //
    macroTilesPerSlice = macroTilesPerRow * (height / macroTileHeight);

    //
    // Compute the slice size.
    //
    sliceBytes = macroTilesPerSlice * macroTileBytes;

    //
    // Compute the slice offset.
    //
    sliceOffset = sliceBytes * (tileSplitSlice + slicesPerTile * (slice / microTileThickness));

    //
    // Compute tile offest
    //
    tileRowIndex = (y / MicroTileHeight) % bankHeight;
    tileColumnIndex = ((x / MicroTileWidth) / numPipes) % bankWidth;
    tileIndex = (tileRowIndex * bankWidth) + tileColumnIndex;
    tileOffset = tileIndex * microTileBytes;

    //
    // Combine the slice offset and macro tile offset with the pixel
    // and sample offsets, accounting for the pipe and bank bits in
    // the middle of the address.
    //
    totalOffset = sliceOffset + macroTileOffset + elementOffset + tileOffset;

    //
    // Get the pipe and bank.
    //

    // when the tileMode is PRT type, then adjust x and y
    // coordinates
    if (gpaIsPrt(arrayMode)) {
        x = x % macroTilePitch;
        y = y % macroTileHeight;
    }

    err = ComputePipeFromCoord(x, y, slice, arrayMode, pipeSwizzle, pTileInfo, &pipe);
    if (err != GPA_ERR_OK) {
        return err;
    }

    err =
        ComputeBankFromCoord(x, y, slice, arrayMode, bankSwizzle, tileSplitSlice, pTileInfo, &bank);
    if (err != GPA_ERR_OK) {
        return err;
    }

    //
    // Split the offset to put some bits below the pipe+bank bits
    // and some above.
    //
    pipeInterleaveMask = (1 << numPipeInterleaveBits) - 1;
    bankInterleaveMask = (1 << numBankInterleaveBits) - 1;
    pipeInterleaveOffset = totalOffset & pipeInterleaveMask;
    bankInterleaveOffset = (uint32_t)((totalOffset >> numPipeInterleaveBits) & bankInterleaveMask);
    offset = totalOffset >> (numPipeInterleaveBits + numBankInterleaveBits);

    //
    // Assemble the address from its components.
    //
    addr = pipeInterleaveOffset;
    // This is to remove /analyze warnings
    uint32_t pipeBits = pipe << numPipeInterleaveBits;
    uint32_t bankInterleaveBits = bankInterleaveOffset << (numPipeInterleaveBits + numPipeBits);
    uint32_t bankBits = bank << (numPipeInterleaveBits + numPipeBits + numBankInterleaveBits);
    uint64_t offsetBits =
        offset << (numPipeInterleaveBits + numPipeBits + numBankInterleaveBits + numBankBits);

    addr |= pipeBits;
    addr |= bankInterleaveBits;
    addr |= bankBits;
    addr |= offsetBits;

    *pBytePosition = addr;
    return GPA_ERR_OK;
}

static GpaError gpaComputeSurfaceOffset(uint64_t* outoffset, uint64_t* outbitoffset,
                                        const GpaTilerContext* ctx, uint32_t x, uint32_t y,
                                        uint32_t z, uint32_t fragindex) {
    if (x > ctx->paddedwidth || y > ctx->paddedheight || z > ctx->paddeddepth ||
        fragindex > ctx->numfragsperpixel) {
        return GPA_ERR_INVALID_ARGS;
    }

    const GnmArrayMode arraymode = gpaGetArrayMode(ctx->tilemode);
    const GnmMicroTileMode microTileType = gpaGetMicroTileMode(ctx->tilemode);
    // ADDR_DEPTH_SAMPLE_ORDER = non-disp + depth-sample-order
    const bool isDepthSampleOrder = microTileType == GNM_SURF_DEPTH_MICRO_TILING;

    if (ctx->mingpumode == GNM_GPU_NEO) {
        /// @note
        /// 128 bit/thick tiled surface doesn't support display
        /// tiling and mipmap chain must have the same tileType,
        /// so please fill tileType correctly
        if (!gpaIsLinear(arraymode)) {
            if (ctx->bitsperelement >= 128 || gpaGetMicroTileThickness(arraymode) > 1) {
                if (microTileType == GNM_SURF_DISPLAY_MICRO_TILING) {
                    return GPA_ERR_INTERNAL_ERROR;
                }
            }
        }
    }

    GpaError err = GPA_ERR_OK;
    uint32_t bitPosition = 0;
    uint64_t addr = 0;

    switch (arraymode) {
    case GNM_ARRAY_LINEAR_GENERAL: // fall through
    case GNM_ARRAY_LINEAR_ALIGNED:
        addr = gpaComputeSurfaceAddrFromCoordLinear(x, y, z, fragindex, ctx->bitsperelement,
                                                    ctx->paddedwidth, ctx->paddedheight,
                                                    ctx->paddeddepth, &bitPosition);
        break;
    case GNM_ARRAY_1D_TILED_THIN1: // fall through
    case GNM_ARRAY_1D_TILED_THICK:
        err = ComputeSurfaceAddrFromCoordMicroTiled(
            x, y, z, fragindex, ctx->bitsperelement, ctx->paddedwidth, ctx->paddedheight,
            ctx->paddeddepth, arraymode, microTileType, isDepthSampleOrder, &addr, &bitPosition);
        break;
    case GNM_ARRAY_2D_TILED_THIN1:     // fall through
    case GNM_ARRAY_2D_TILED_THICK:     // fall through
    case GNM_ARRAY_3D_TILED_THIN1:     // fall through
    case GNM_ARRAY_3D_TILED_THICK:     // fall through
    case GNM_ARRAY_2D_TILED_XTHICK:    // fall through
    case GNM_ARRAY_3D_TILED_XTHICK:    // fall through
    case GNM_ARRAY_PRT_TILED_THIN1:    // fall through
    case GNM_ARRAY_PRT_2D_TILED_THIN1: // fall through
    case GNM_ARRAY_PRT_3D_TILED_THIN1: // fall through
    case GNM_ARRAY_PRT_TILED_THICK:    // fall through
    case GNM_ARRAY_PRT_2D_TILED_THICK: // fall through
    case GNM_ARRAY_PRT_3D_TILED_THICK:
        err = ComputeSurfaceAddrFromCoordMacroTiled(
            x, y, z, fragindex, ctx->bitsperelement, ctx->paddedwidth, ctx->paddedheight,
            ctx->paddeddepth, arraymode, microTileType, isDepthSampleOrder, ctx->pipeswizzlemask,
            ctx->bankswizzlemask, &ctx->tileinfo, &addr, &bitPosition);
        break;
    default:
        err = GPA_ERR_INTERNAL_ERROR;
        break;
    }

    if (outoffset) {
        *outoffset = addr;
    }
    if (outbitoffset) {
        *outbitoffset = bitPosition;
    }
    return err;
}

GpaError gpaTpInit(GpaTilingParams* tp, const GpaTextureInfo* tex, uint32_t miplevel,
                   uint32_t arrayslice) {
    if (!tp || !tex) {
        return GPA_ERR_INVALID_ARGS;
    }
    if (miplevel > tex->nummips) {
        return GPA_ERR_INVALID_ARGS;
    }

    const bool iscubemap = tex->type == GNM_TEXTURE_CUBEMAP;
    const bool isvolume = tex->type == GNM_TEXTURE_3D;
    const GnmDataFormat fmt = tex->fmt;
    const GnmMicroTileMode mtm = gpaGetMicroTileMode(tex->tm);
    const uint32_t totalbitsperelem = gnmDfGetTotalBitsPerElement(fmt);
    const uint32_t texelsperelem = gnmDfGetTexelsPerElement(fmt);

    uint32_t numarrayslices = tex->numslices;
    if (tex->type == GNM_TEXTURE_CUBEMAP) {
        numarrayslices *= 6;
    } else if (tex->type == GNM_TEXTURE_3D) {
        numarrayslices = 1;
    }
    if (tex->pow2pad) {
        numarrayslices = NextPow2(numarrayslices);
    }

    if (arrayslice >= numarrayslices) {
        return GPA_ERR_INVALID_ARGS;
    }

    tp->tilemode = tex->tm;
    tp->mingpumode = tex->mingpumode;

    tp->linearwidth = std::max(tex->width >> miplevel, 1U);
    tp->linearheight = std::max(tex->height >> miplevel, 1U);
    tp->lineardepth = std::max(tex->depth >> miplevel, 1U);
    tp->numfragsperpixel = tex->numfrags;
    tp->basetiledpitch = tex->pitch;

    tp->miplevel = miplevel;
    tp->arrayslice = arrayslice;

    if (!isvolume && mtm == GNM_SURF_DEPTH_MICRO_TILING) {
        if (gnmDfGetZFormat(fmt) != GNM_Z_INVALID) {
            tp->surfaceflags.depthtarget = 1;
        }
        if (gnmDfGetStencilFormat(fmt) != GNM_STENCIL_INVALID) {
            tp->surfaceflags.stenciltarget = 1;
        }
    }
    tp->surfaceflags.cube = iscubemap;
    tp->surfaceflags.volume = isvolume;
    tp->surfaceflags.pow2pad = tex->pow2pad;
    if (tex->mingpumode == GNM_GPU_NEO) {
        tp->surfaceflags.texcompatible = 1;
    }

    tp->bitsperfrag = totalbitsperelem / texelsperelem;
    tp->isblockcompressed = texelsperelem > 1;

    GpaSurfaceInfo surfinfo = {0};
    GpaError err = gpaComputeSurfaceInfo(&surfinfo, tp);
    if (err != GPA_ERR_OK) {
        return err;
    }
    err = gpaAdjustTileMode(&tp->tilemode, tp->tilemode, surfinfo.tileinfo.arraymode);
    if (err != GPA_ERR_OK) {
        return err;
    }

    return GPA_ERR_OK;
}

static GpaError initregioninfo(GpaSurfaceRegion* region, uint32_t* elemwidth, uint32_t* elemheight,
                               const GpaTilingParams* tp) {
    region->right = tp->linearwidth;
    region->bottom = tp->linearheight;
    region->back = tp->lineardepth;
    *elemwidth = tp->linearwidth;
    *elemheight = tp->linearheight;

    if (tp->isblockcompressed) {
        switch (tp->bitsperfrag) {
        case 1:
            region->left = (region->left + 7) / 8;
            region->right = (region->right + 7) / 8;
            *elemwidth = (*elemwidth + 7) / 8;
            break;
        case 4:
        case 8:
            region->left = (region->left + 3) / 4;
            region->top = (region->top + 3) / 4;
            region->right = (region->right + 3) / 4;
            region->bottom = (region->bottom + 3) / 4;
            *elemwidth = (*elemwidth + 3) / 4;
            *elemheight = (*elemheight + 3) / 4;
            break;
        case 16:
            return GPA_ERR_UNSUPPORTED;
        default:
            return GPA_ERR_INVALID_ARGS;
        }
    }

    return GPA_ERR_OK;
}

GpaError gpaTileSurface(void* outtile, size_t outtilesize, const void* inuntile,
                        size_t inuntilesize, const GpaTilingParams* tp) {
    if (!outtile || !outtilesize || !inuntile || !inuntilesize || !tp) {
        return GPA_ERR_INVALID_ARGS;
    }

    GpaSurfaceRegion region = {0};
    uint32_t elemwidth = 0;
    uint32_t elemheight = 0;
    initregioninfo(&region, &elemwidth, &elemheight, tp);

    return gpaTileSurfaceRegion(outtile, outtilesize, inuntile, inuntilesize, tp, &region,
                                elemwidth, elemwidth * elemheight);
}

static inline bool regionhastexels(const GpaSurfaceRegion* region) {
    const uint32_t width = region->right - region->left;
    const uint32_t height = region->bottom - region->top;
    const uint32_t depth = region->back - region->top;
    return width > 0 && height > 0 && depth > 0;
}

GpaError gpaTileSurfaceRegion(void* outtile, size_t outtilesize, const void* inuntile,
                              size_t inuntilesize, const GpaTilingParams* tp,
                              const GpaSurfaceRegion* region, uint32_t srcpitch,
                              uint32_t srcslicepitch) {
    if (!outtile || !outtilesize || !inuntile || !inuntilesize || !tp || !region) {
        return GPA_ERR_INVALID_ARGS;
    }

    if (!regionhastexels(region)) {
        // nothing to convert
        return GPA_ERR_OK;
    }

    GpaTilerContext ctx = {};
    GpaError err = createtilerctx(&ctx, outtilesize, inuntilesize, tp);
    if (err != GPA_ERR_OK) {
        return err;
    }
    const uint32_t elembytesize = ctx.bitsperelement / 8;
    const uint32_t lz = region->back;
    const uint32_t ly = region->bottom;
    const uint32_t lx = region->right;
    const uint32_t lf = ctx.numfragsperpixel;
    for (uint32_t z = region->front; z < lz; z += 1) {
        for (uint32_t y = region->top; y < ly; y += 1) {
            for (uint32_t x = region->left; x < lx; x += 1) {
                for (uint32_t f = 0; f < lf; f += 1) {
                    const uint32_t linearoffset = ComputeSurfaceAddrFromCoordLinear(
                        x, y, z, f, elembytesize * 8, srcpitch, srcslicepitch, 1, NULL);
                    uint64_t tiledoffset = 0;
                    GpaError gerr = gpaComputeSurfaceOffset(&tiledoffset, NULL, &ctx, x, y, z, f);
                    if (gerr != GPA_ERR_OK) {
                        return gerr;
                    }

                    memcpy((uint8_t*)outtile + tiledoffset, (const uint8_t*)inuntile + linearoffset,
                           elembytesize);
                }
            }
        }
    }

    return GPA_ERR_OK;
}

GpaError gpaDetileSurface(void* outuntile, size_t outuntilesize, const void* intile,
                          size_t intilesize, const GpaTilingParams* tp) {
    if (!outuntile || !outuntilesize || !intile || !intilesize || !tp) {
        return GPA_ERR_INVALID_ARGS;
    }

    GpaSurfaceRegion region = {0};
    uint32_t elemwidth = 0;
    uint32_t elemheight = 0;
    initregioninfo(&region, &elemwidth, &elemheight, tp);

    return gpaDetileSurfaceRegion(outuntile, outuntilesize, intile, intilesize, tp, &region,
                                  elemwidth, elemwidth * elemheight);
}

GpaError gpaDetileSurfaceRegion(void* outuntile, size_t outuntilesize, const void* intile,
                                size_t intilesize, const GpaTilingParams* tp,
                                const GpaSurfaceRegion* region, uint32_t dstpitch,
                                uint32_t dstslicepitch) {
    if (!outuntile || !outuntilesize || !intile || !intilesize || !tp || !region) {
        return GPA_ERR_INVALID_ARGS;
    }

    if (!regionhastexels(region)) {
        // nothing to convert
        return GPA_ERR_OK;
    }

    GpaTilerContext ctx = {};
    GpaError err = createtilerctx(&ctx, outuntilesize, intilesize, tp);
    if (err != GPA_ERR_OK) {
        return err;
    }

    const uint32_t elembytesize = ctx.bitsperelement / 8;
    const uint32_t lz = region->back;
    const uint32_t ly = region->bottom;
    const uint32_t lx = region->right;
    const uint32_t lf = ctx.numfragsperpixel;
    for (uint32_t z = region->front; z < lz; z += 1) {
        for (uint32_t y = region->top; y < ly; y += 1) {
            for (uint32_t x = region->left; x < lx; x += 1) {
                for (uint32_t f = 0; f < lf; f += 1) {
                    const uint32_t linearoffset = ComputeSurfaceAddrFromCoordLinear(
                        x, y, z, f, elembytesize * 8, dstpitch, dstslicepitch, 1, NULL);
                    uint64_t tiledoffset = 0;
                    GpaError gerr = gpaComputeSurfaceOffset(&tiledoffset, NULL, &ctx, x, y, z, f);
                    if (gerr != GPA_ERR_OK) {
                        return gerr;
                    }

                    memcpy((uint8_t*)outuntile + linearoffset, (const uint8_t*)intile + tiledoffset,
                           elembytesize);
                }
            }
        }
    }

    return GPA_ERR_OK;
}

GpaError gpaTileTextureIndexed(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                               size_t outbuffersize, const GpaTextureInfo* texinfo, uint32_t mip,
                               uint32_t slice) {
    if (!inbuffer || !inbuffersize || !outbuffer || !outbuffersize || !texinfo) {
        return GPA_ERR_INVALID_ARGS;
    }

    GpaTilingParams tp = {};
    GpaError res = gpaTpInit(&tp, texinfo, mip, slice);
    if (res != GPA_ERR_OK) {
        return res;
    }

    uint64_t surfoffset = 0;
    uint64_t surfsize = 0;
    res = gpaCalcSurfaceSizeOffset(&surfsize, &surfoffset, texinfo, mip, slice);
    if (res != GPA_ERR_OK) {
        return res;
    }
    if (surfoffset + surfsize > inbuffersize || surfoffset + surfsize > outbuffersize) {
        return GPA_ERR_OVERFLOW;
    }

    res = gpaTileSurface((uint8_t*)outbuffer + surfoffset, surfsize,
                         (const uint8_t*)inbuffer + surfoffset, surfsize, &tp);
    if (res != GPA_ERR_OK) {
        return res;
    }

    return GPA_ERR_OK;
}

GpaError gpaTileTextureAll(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                           size_t outbuffersize, const GpaTextureInfo* texinfo) {
    if (!inbuffer || !inbuffersize || !outbuffer || !outbuffersize || !texinfo) {
        return GPA_ERR_INVALID_ARGS;
    }

    for (uint32_t a = 0; a < texinfo->numslices; a += 1) {
        for (uint32_t m = 0; m < texinfo->nummips; m += 1) {
            GpaError gerr = gpaTileTextureIndexed(inbuffer, inbuffersize, outbuffer, outbuffersize,
                                                  texinfo, m, a);
            if (gerr != GPA_ERR_OK) {
                return gerr;
            }
        }
    }

    return GPA_ERR_OK;
}

GpaError gpaDetileTextureIndexed(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                                 size_t outbuffersize, const GpaTextureInfo* texinfo, uint32_t mip,
                                 uint32_t slice) {
    if (!inbuffer || !inbuffersize || !outbuffer || !outbuffersize || !texinfo) {
        return GPA_ERR_INVALID_ARGS;
    }

    GpaTilingParams tp = {};
    GpaError res = gpaTpInit(&tp, texinfo, mip, slice);
    if (res != GPA_ERR_OK) {
        return res;
    }

    uint64_t surfoffset = 0;
    uint64_t surfsize = 0;
    res = gpaCalcSurfaceSizeOffset(&surfsize, &surfoffset, texinfo, mip, slice);
    if (res != GPA_ERR_OK) {
        return res;
    }
    if (surfoffset + surfsize > inbuffersize || surfoffset + surfsize > outbuffersize) {
        return GPA_ERR_OVERFLOW;
    }

    res = gpaDetileSurface((uint8_t*)outbuffer + surfoffset, surfsize,
                           (const uint8_t*)inbuffer + surfoffset, surfsize, &tp);
    if (res != GPA_ERR_OK) {
        return res;
    }

    return GPA_ERR_OK;
}

GpaError gpaDetileTextureAll(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                             size_t outbuffersize, const GpaTextureInfo* texinfo) {
    if (!inbuffer || !inbuffersize || !outbuffer || !outbuffersize || !texinfo) {
        return GPA_ERR_INVALID_ARGS;
    }

    for (uint32_t a = 0; a < texinfo->numslices; a += 1) {
        for (uint32_t m = 0; m < texinfo->nummips; m += 1) {
            GpaError gerr = gpaDetileTextureIndexed(inbuffer, inbuffersize, outbuffer,
                                                    outbuffersize, texinfo, m, a);
            if (gerr != GPA_ERR_OK) {
                return gerr;
            }
        }
    }

    return GPA_ERR_OK;
}
