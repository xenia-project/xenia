/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_H_
#define XENIA_GPU_TEXTURE_H_

#include <xenia/core.h>
#include <xenia/gpu/xenos/xenos.h>

// TODO(benvanik): replace DXGI constants with xenia constants.
#include <d3d11.h>


namespace xe {
namespace gpu {


class Texture;

struct TextureSizes1D {};
struct TextureSizes2D {
  uint32_t logical_width;
  uint32_t logical_height;
  uint32_t block_width;
  uint32_t block_height;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
  uint32_t logical_pitch;
  uint32_t input_pitch;
};
struct TextureSizes3D {};
struct TextureSizesCube {};

struct TextureView {
  Texture* texture;
  xenos::xe_gpu_texture_fetch_t fetch;
  uint64_t hash;

  union {
    TextureSizes1D sizes_1d;
    TextureSizes2D sizes_2d;
    TextureSizes3D sizes_3d;
    TextureSizesCube sizes_cube;
  };

  int dimensions;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t block_size;
  uint32_t texel_pitch;
  bool is_compressed;
  DXGI_FORMAT format;

  TextureView()
    : texture(nullptr),
      dimensions(0),
      width(0), height(0), depth(0),
      block_size(0), texel_pitch(0),
      is_compressed(false), format(DXGI_FORMAT_UNKNOWN) {}
};


class Texture {
public:
  Texture(uint32_t address, const uint8_t* host_address);
  virtual ~Texture();

  TextureView* Fetch(
      const xenos::xe_gpu_texture_fetch_t& fetch);

protected:
  bool FillViewInfo(TextureView* view,
                    const xenos::xe_gpu_texture_fetch_t& fetch);

  virtual TextureView* FetchNew(
      const xenos::xe_gpu_texture_fetch_t& fetch) = 0;
  virtual bool FetchDirty(
      TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) = 0;

  const TextureSizes2D GetTextureSizes2D(TextureView* view);

  static void TextureSwap(uint8_t* dest, const uint8_t* src, uint32_t pitch,
                          xenos::XE_GPU_ENDIAN endianness);
  static uint32_t TiledOffset2DOuter(uint32_t y, uint32_t width,
                                     uint32_t log_bpp);
  static uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                     uint32_t base_offset);

  uint32_t address_;
  const uint8_t* host_address_;

  // TODO(benvanik): replace with LRU keyed list.
  std::vector<TextureView*> views_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_TEXTURE_H_
