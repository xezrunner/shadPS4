// SPDX-FileCopyrightText: Copyright 2024 freegnm Project
// SPDX-License-Identifier: MIT

#pragma once

#include "video_core/amdgpu/gpuaddr/error.h"
#include "video_core/amdgpu/gpuaddr/types.h"

//
// Surface
//
GpaError gpaComputeSurfaceInfo(GpaSurfaceInfo* out, const GpaTilingParams* tp);
GpaError gpaComputeHtileInfo(GpaHtileInfo* outinfo, const GpaHtileParams* params);
GpaError gpaComputeCmaskInfo(GpaCmaskInfo* outinfo, const GpaCmaskParams* params);
GpaError gpaComputeFmaskInfo(GpaFmaskInfo* outinfo, const GpaFmaskParams* params);
GpaError gpaComputeSurfaceTileMode(GnmTileMode* outtilemode, GnmGpuMode mingpumode,
                                   GnmArrayMode arraymode, GpaSurfaceFlags flags,
                                   GnmDataFormat surfacefmt, u32 numfragsperpixel,
                                   GnmMicroTileMode mtm);

//
// Surface generation
//
GpaError gpaFindOptimalSurface(GpaSurfaceProperties* outprops, GpaSurfaceType surfacetype, u32 bpp,
                               u32 numfrags, bool mipmapped, GnmGpuMode mingpumode);

//
// Element/Utility
//
uint64_t gpaComputeSurfaceAddrFromCoordLinear(u32 x, u32 y, u32 slice, u32 sample, u32 bpp,
                                              u32 pitch, u32 height, u32 numSlices,
                                              u32* pBitPosition);
GpaError gpaCalcSurfaceSizeOffset(uint64_t* outsize, uint64_t* outoffset, const GpaTextureInfo* tex,
                                  u32 miplevel, u32 arrayslice);

GpaError gpaGetTileInfo(GpaTileInfo* outinfo, GnmTileMode tilemode, u32 bpp, u32 numfrags,
                        GnmGpuMode gpumode);
GpaError gpaComputeBaseSwizzle(u32* outswizzle, GnmTileMode tilemode, u32 surfindex, u32 bpp,
                               u32 numfrags, GnmGpuMode gpumode);

//
// Decompression
//
GpaError gpaGetDecompressedSize(uint64_t* outsize, const void* inbuffer, size_t inbuffersize,
                                const GpaTextureInfo* texinfo);
GpaError gpaDecompressTexture(void* outbuffer, uint64_t outbuffersize, const void* inbuffer,
                              uint64_t inbuffersize, const GpaTextureInfo* texinfo,
                              GnmDataFormat* outfmt);

//
// Tiler
//
GpaError gpaTpInit(GpaTilingParams* tp, const GpaTextureInfo* tex, u32 miplevel, u32 arrayslice);

GpaError gpaTileSurface(void* outtile, size_t outtilesize, const void* inuntile,
                        size_t inuntilesize, const GpaTilingParams* tp);
GpaError gpaTileSurfaceRegion(void* outtile, size_t outtilesize, const void* inuntile,
                              size_t inuntilesize, const GpaTilingParams* tp,
                              const GpaSurfaceRegion* region, u32 srcpitch, u32 srcslicepitch);
GpaError gpaTileTextureIndexed(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                               size_t outbuffersize, const GpaTextureInfo* texinfo, u32 mip,
                               u32 slice);
GpaError gpaTileTextureAll(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                           size_t outbuffersize, const GpaTextureInfo* texinfo);
GpaError gpaDetileSurface(void* outuntile, size_t outuntilesize, const void* intile,
                          size_t intilesize, const GpaTilingParams* tp);
GpaError gpaDetileSurfaceRegion(void* outuntile, size_t outuntilesize, const void* intile,
                                size_t intilesize, const GpaTilingParams* tp,
                                const GpaSurfaceRegion* region, u32 dstpitch, u32 dstslicepitch);
GpaError gpaDetileTextureIndexed(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                                 size_t outbuffersize, const GpaTextureInfo* texinfo, u32 mip,
                                 u32 slice);
GpaError gpaDetileTextureAll(const void* inbuffer, size_t inbuffersize, void* outbuffer,
                             size_t outbuffersize, const GpaTextureInfo* texinfo);
