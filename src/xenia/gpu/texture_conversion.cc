/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_conversion.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"

#include "third_party/xxhash/xxhash.h"

namespace xe {
namespace gpu {
namespace texture_conversion {

using namespace xe::gpu::xenos;

void CopySwapBlock(Endian endian, void* output, const void* input,
                   size_t length) {
  switch (endian) {
    case Endian::k8in16:
      xe::copy_and_swap_16_unaligned(output, input, length / 2);
      break;
    case Endian::k8in32:
      xe::copy_and_swap_32_unaligned(output, input, length / 4);
      break;
    case Endian::k16in32:  // Swap high and low 16 bits within a 32 bit word
      xe::copy_and_swap_16_in_32_unaligned(output, input, length);
      break;
    default:
    case Endian::kUnspecified:
      std::memcpy(output, input, length);
      break;
  }
}

void ConvertTexelCTX1ToR8G8(Endian endian, void* output, const void* input,
                            size_t length) {
  // https://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
  // (R is in the higher bits, according to how this format is used in Halo 3).
  union {
    uint8_t data[8];
    struct {
      uint8_t g0, r0, g1, r1;
      uint32_t xx;
    };
  } block;
  static_assert(sizeof(block) == 8, "CTX1 block mismatch");

  const uint32_t bytes_per_block = 8;
  CopySwapBlock(endian, block.data, input, bytes_per_block);

  uint8_t cr[4] = {
      block.r0, block.r1,
      static_cast<uint8_t>(2.f / 3.f * block.r0 + 1.f / 3.f * block.r1),
      static_cast<uint8_t>(1.f / 3.f * block.r0 + 2.f / 3.f * block.r1)};
  uint8_t cg[4] = {
      block.g0, block.g1,
      static_cast<uint8_t>(2.f / 3.f * block.g0 + 1.f / 3.f * block.g1),
      static_cast<uint8_t>(1.f / 3.f * block.g0 + 2.f / 3.f * block.g1)};

  auto output_bytes = static_cast<uint8_t*>(output);
  for (uint32_t oy = 0; oy < 4; ++oy) {
    for (uint32_t ox = 0; ox < 4; ++ox) {
      uint8_t xx = (block.xx >> (((ox + (oy * 4)) * 2))) & 3;
      output_bytes[(oy * length) + (ox * 2) + 0] = cr[xx];
      output_bytes[(oy * length) + (ox * 2) + 1] = cg[xx];
    }
  }
}

void ConvertTexelDXT3AToDXT3(Endian endian, void* output, const void* input,
                             size_t length) {
  const uint32_t bytes_per_block = 16;
  auto output_bytes = static_cast<uint8_t*>(output);
  CopySwapBlock(endian, &output_bytes[0], input, 8);
  std::memset(&output_bytes[8], 0, 8);
}

// https://github.com/BinomialLLC/crunch/blob/ea9b8d8c00c8329791256adafa8cf11e4e7942a2/inc/crn_decomp.h#L4108
static uint32_t TiledOffset2DRow(uint32_t y, uint32_t width,
                                 uint32_t log2_bpp) {
  uint32_t macro = ((y / 32) * (width / 32)) << (log2_bpp + 7);
  uint32_t micro = ((y & 6) << 2) << log2_bpp;
  return macro + ((micro & ~0xF) << 1) + (micro & 0xF) +
         ((y & 8) << (3 + log2_bpp)) + ((y & 1) << 4);
}

static uint32_t TiledOffset2DColumn(uint32_t x, uint32_t y, uint32_t log2_bpp,
                                    uint32_t base_offset) {
  uint32_t macro = (x / 32) << (log2_bpp + 7);
  uint32_t micro = (x & 7) << log2_bpp;
  uint32_t offset =
      base_offset + (macro + ((micro & ~0xF) << 1) + (micro & 0xF));
  return ((offset & ~0x1FF) << 3) + ((offset & 0x1C0) << 2) + (offset & 0x3F) +
         ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

void Untile(uint8_t* output_buffer, const uint8_t* input_buffer,
            const UntileInfo* untile_info) {
  SCOPE_profile_cpu_f("gpu");
  assert_not_null(untile_info);
  assert_not_null(untile_info->input_format_info);
  assert_not_null(untile_info->output_format_info);

  uint32_t input_bytes_per_block =
      untile_info->input_format_info->bytes_per_block();
  uint32_t output_bytes_per_block =
      untile_info->output_format_info->bytes_per_block();
  uint32_t output_pitch = untile_info->output_pitch * output_bytes_per_block;

  // Bytes per pixel
  auto log2_bpp = (input_bytes_per_block / 4) +
                  ((input_bytes_per_block / 2) >> (input_bytes_per_block / 4));

  // Offset to the current row, in bytes.
  uint32_t output_row_offset = 0;
  for (uint32_t y = 0; y < untile_info->height; y++) {
    auto input_row_offset = TiledOffset2DRow(
        untile_info->offset_y + y, untile_info->input_pitch, log2_bpp);

    // Go block-by-block on this row.
    uint32_t output_offset = output_row_offset;

    for (uint32_t x = 0; x < untile_info->width; x++) {
      auto input_offset = TiledOffset2DColumn(untile_info->offset_x + x,
                                              untile_info->offset_y + y,
                                              log2_bpp, input_row_offset);
      input_offset >>= log2_bpp;

      untile_info->copy_callback(
          &output_buffer[output_offset],
          &input_buffer[input_offset * input_bytes_per_block],
          output_bytes_per_block);

      output_offset += output_bytes_per_block;
    }

    output_row_offset += output_pitch;
  }
}

}  //  namespace texture_conversion
}  //  namespace gpu
}  //  namespace xe
