/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_RESOURCE_H_
#define XENIA_GPU_TEXTURE_RESOURCE_H_

#include <xenia/gpu/resource.h>
#include <xenia/gpu/xenos/xenos.h>

// TODO(benvanik): replace DXGI constants with xenia constants.
#include <d3d11.h>


namespace xe {
namespace gpu {


enum TextureDimension {
  TEXTURE_DIMENSION_1D = 0,
  TEXTURE_DIMENSION_2D = 1,
  TEXTURE_DIMENSION_3D = 2,
  TEXTURE_DIMENSION_CUBE = 3,
};


class TextureResource : public PagedResource {
public:
  struct Info {
    TextureDimension dimension;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t block_size;
    uint32_t texel_pitch;
    xenos::XE_GPU_ENDIAN endianness;
    bool is_tiled;
    bool is_compressed;
    uint32_t input_length;

    // TODO(benvanik): replace with our own constants.
    DXGI_FORMAT format;

    union {
      struct {
        uint32_t width;
      } size_1d;
      struct {
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
      } size_2d;
      struct {
      } size_3d;
      struct {
      } size_cube;
    };

    static bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                        Info& out_info);

  private:
    void CalculateTextureSizes1D(const xenos::xe_gpu_texture_fetch_t& fetch);
    void CalculateTextureSizes2D(const xenos::xe_gpu_texture_fetch_t& fetch);
  };

  TextureResource(const MemoryRange& memory_range,
                  const Info& info);
  ~TextureResource() override;

  const Info& info() const { return info_; }

  bool Equals(const void* info_ptr, size_t info_length) override {
    return info_length == sizeof(Info) &&
           memcmp(info_ptr, &info_, info_length) == 0;
  }
  
  virtual int Prepare();

protected:
  virtual int CreateHandle() = 0;
  virtual int InvalidateRegion(const MemoryRange& memory_range) = 0;

  void TextureSwap(uint8_t* dest, const uint8_t* src, uint32_t pitch) const;
  uint32_t TiledOffset2DOuter(uint32_t y, uint32_t width,
                              uint32_t log_bpp) const;
  uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                              uint32_t base_offset) const;

  Info info_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_TEXTURE_RESOURCE_H_
