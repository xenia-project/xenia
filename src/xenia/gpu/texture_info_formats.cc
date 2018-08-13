/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// clang-format off

#include "xenia/gpu/texture_info.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

#define FORMAT_INFO(texture_format, format, block_width, block_height, bits_per_pixel) \
    {TextureFormat::texture_format, #texture_format, FormatType::format, block_width, block_height, bits_per_pixel}
const FormatInfo* FormatInfo::Get(uint32_t gpu_format) {
  static const FormatInfo format_infos[64] = {
      FORMAT_INFO(k_1_REVERSE                , kUncompressed, 1, 1, 1),
      FORMAT_INFO(k_1                        , kUncompressed, 1, 1, 1),
      FORMAT_INFO(k_8                        , kUncompressed, 1, 1, 8),
      FORMAT_INFO(k_1_5_5_5                  , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_5_6_5                    , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_6_5_5                    , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_8_8_8_8                  , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_2_10_10_10               , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_8_A                      , kUncompressed, 1, 1, 8),
      FORMAT_INFO(k_8_B                      , kUncompressed, 1, 1, 8),
      FORMAT_INFO(k_8_8                      , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_Cr_Y1_Cb_Y0_REP          , kCompressed  , 2, 1, 16),
      FORMAT_INFO(k_Y1_Cr_Y0_Cb_REP          , kCompressed  , 2, 1, 16),
      FORMAT_INFO(k_16_16_EDRAM              , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_8_8_8_8_A                , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_4_4_4_4                  , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_10_11_11                 , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_11_11_10                 , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_DXT1                     , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_DXT2_3                   , kCompressed  , 4, 4, 8),
      FORMAT_INFO(k_DXT4_5                   , kCompressed  , 4, 4, 8),
      FORMAT_INFO(k_16_16_16_16_EDRAM        , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_24_8                     , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_24_8_FLOAT               , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_16                       , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_16                    , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_16_16_16_16              , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_16_EXPAND                , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_16_EXPAND             , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_16_16_16_16_EXPAND       , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_16_FLOAT                 , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_16_FLOAT              , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_16_16_16_16_FLOAT        , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_32                       , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_32_32                    , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_32_32_32_32              , kUncompressed, 1, 1, 128),
      FORMAT_INFO(k_32_FLOAT                 , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_32_32_FLOAT              , kUncompressed, 1, 1, 64),
      FORMAT_INFO(k_32_32_32_32_FLOAT        , kUncompressed, 1, 1, 128),
      FORMAT_INFO(k_32_AS_8                  , kCompressed  , 4, 1, 8),
      FORMAT_INFO(k_32_AS_8_8                , kCompressed  , 2, 1, 16),
      FORMAT_INFO(k_16_MPEG                  , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_16_MPEG               , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_8_INTERLACED             , kUncompressed, 1, 1, 8),
      FORMAT_INFO(k_32_AS_8_INTERLACED       , kCompressed  , 4, 1, 8),
      FORMAT_INFO(k_32_AS_8_8_INTERLACED     , kCompressed  , 1, 1, 16),
      FORMAT_INFO(k_16_INTERLACED            , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_MPEG_INTERLACED       , kUncompressed, 1, 1, 16),
      FORMAT_INFO(k_16_16_MPEG_INTERLACED    , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_DXN                      , kCompressed  , 4, 4, 8),
      FORMAT_INFO(k_8_8_8_8_AS_16_16_16_16   , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_DXT1_AS_16_16_16_16      , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_DXT2_3_AS_16_16_16_16    , kCompressed  , 4, 4, 8),
      FORMAT_INFO(k_DXT4_5_AS_16_16_16_16    , kCompressed  , 4, 4, 8),
      FORMAT_INFO(k_2_10_10_10_AS_16_16_16_16, kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_10_11_11_AS_16_16_16_16  , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_11_11_10_AS_16_16_16_16  , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_32_32_32_FLOAT           , kUncompressed, 1, 1, 96),
      FORMAT_INFO(k_DXT3A                    , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_DXT5A                    , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_CTX1                     , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_DXT3A_AS_1_1_1_1         , kCompressed  , 4, 4, 4),
      FORMAT_INFO(k_8_8_8_8_GAMMA_EDRAM      , kUncompressed, 1, 1, 32),
      FORMAT_INFO(k_2_10_10_10_FLOAT_EDRAM   , kUncompressed, 1, 1, 32),
  };
  return &format_infos[gpu_format];
}
#undef FORMAT_INFO

}  //  namespace gpu
}  //  namespace xe
