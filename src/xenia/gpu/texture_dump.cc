/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/texture_info.h"

DEFINE_bool(texture_dump, false, "Dump textures to DDS", "GPU");

namespace xe {
namespace gpu {

void TextureDump(const TextureInfo& src, void* buffer, size_t length) {
  struct {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch_or_linear_size;
    uint32_t depth;
    uint32_t mip_levels;
    uint32_t reserved1[11];
    struct {
      uint32_t size;
      uint32_t flags;
      be<fourcc_t> fourcc;
      uint32_t rgb_bit_count;
      uint32_t r_bit_mask;
      uint32_t g_bit_mask;
      uint32_t b_bit_mask;
      uint32_t a_bit_mask;
    } pixel_format;
    uint32_t caps[4];
    uint32_t reserved2;
  } dds_header;

  std::memset(&dds_header, 0, sizeof(dds_header));
  dds_header.size = sizeof(dds_header);
  dds_header.flags = 1u | 2u | 4u | 0x1000u | 0x20000u;
  if (src.is_compressed()) {
    dds_header.flags |= 0x80000u;
  } else {
    dds_header.flags |= 0x8u;
  }
  dds_header.height = std::max(1u, (src.height + 1) >> src.mip_min_level);
  dds_header.width = std::max(1u, (src.width + 1) >> src.mip_min_level);
  dds_header.pitch_or_linear_size = src.GetMipExtent(0, false).block_pitch_h *
                                    src.format_info()->bytes_per_block();
  dds_header.mip_levels = src.mip_levels();

  dds_header.pixel_format.size = sizeof(dds_header.pixel_format);
  switch (src.format) {
    case xenos::TextureFormat::k_DXT1: {
      dds_header.pixel_format.flags = 0x4u;
      dds_header.pixel_format.fourcc = make_fourcc("DXT1");
      break;
    }
    case xenos::TextureFormat::k_DXT2_3: {
      dds_header.pixel_format.flags = 0x4u;
      dds_header.pixel_format.fourcc = make_fourcc("DXT3");
      break;
    }
    case xenos::TextureFormat::k_DXT4_5: {
      dds_header.pixel_format.flags = 0x4u;
      dds_header.pixel_format.fourcc = make_fourcc("DXT5");
      break;
    }
    case xenos::TextureFormat::k_8_8_8_8: {
      dds_header.pixel_format.flags = 0x1u | 0x40u;
      dds_header.pixel_format.rgb_bit_count = 32;
      dds_header.pixel_format.r_bit_mask = 0x00FF0000u;
      dds_header.pixel_format.g_bit_mask = 0x0000FF00u;
      dds_header.pixel_format.b_bit_mask = 0x000000FFu;
      dds_header.pixel_format.a_bit_mask = 0xFF000000u;
      break;
    }
    default: {
      assert_unhandled_case(src.format);
      std::memset(&dds_header.pixel_format, 0xCD,
                  sizeof(dds_header.pixel_format));
      XELOGW("Skipping {} for texture dump.", src.format_name());
      return;
    }
  }

  dds_header.caps[0] = 8u | 0x1000u;

  static int dump_counter = 0;
  std::filesystem::path path = "texture_dumps";
  path /= fmt::format(fmt::runtime("{:05d}_{:08X}_{:08X}_{:08X}.dds"),
                      dump_counter++, src.memory.base_address,
                      src.memory.mip_address, src.format_name());

  FILE* handle = filesystem::OpenFile(path, "wb");
  if (handle) {
    const char signature[4] = {'D', 'D', 'S', ' '};
    fwrite(&signature, sizeof(signature), 1, handle);
    fwrite(&dds_header, sizeof(dds_header), 1, handle);
    fwrite(buffer, 1, length, handle);
    fclose(handle);
  }
}

}  // namespace gpu
}  // namespace xe
