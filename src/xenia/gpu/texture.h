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


struct TextureView {
  Texture* texture;
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
  Texture(uint32_t address);
  virtual ~Texture() = default;

  virtual TextureView* Fetch(
      const xenos::xe_gpu_texture_fetch_t& fetch) = 0;

protected:
  bool FillViewInfo(TextureView* view,
                    const xenos::xe_gpu_texture_fetch_t& fetch);

  static void TextureSwap(uint8_t* dest, const uint8_t* src, uint32_t pitch,
                          xenos::XE_GPU_ENDIAN endianness);
  static uint32_t TiledOffset2DOuter(uint32_t y, uint32_t width,
                                     uint32_t log_bpp);
  static uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                     uint32_t base_offset);

  uint32_t address_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_TEXTURE_H_
