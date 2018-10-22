/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/texture_config.h"

namespace xe {
namespace gpu {
namespace vulkan {

#define COMP_SWIZ(r, g, b, a)                              \
  {                                                        \
    VK_COMPONENT_SWIZZLE_##r, VK_COMPONENT_SWIZZLE_##g,    \
        VK_COMPONENT_SWIZZLE_##b, VK_COMPONENT_SWIZZLE_##a \
  }
#define VEC_SWIZ(x, y, z, w)                                    \
  {                                                             \
    VECTOR_SWIZZLE_##x, VECTOR_SWIZZLE_##y, VECTOR_SWIZZLE_##z, \
        VECTOR_SWIZZLE_##w                                      \
  }

#define RGBA COMP_SWIZ(R, G, B, A)
#define ___R COMP_SWIZ(IDENTITY, IDENTITY, IDENTITY, R)
#define RRRR COMP_SWIZ(R, R, R, R)

#define XYZW VEC_SWIZ(X, Y, Z, W)
#define YXWZ VEC_SWIZ(Y, X, W, Z)
#define ZYXW VEC_SWIZ(Z, Y, X, W)

#define ___(format) \
  { VK_FORMAT_##format }
#define _c_(format, component_swizzle) \
  { VK_FORMAT_##format, component_swizzle, XYZW }
#define __v(format, vector_swizzle) \
  { VK_FORMAT_##format, RGBA, vector_swizzle }
#define _cv(format, component_swizzle, vector_swizzle) \
  { VK_FORMAT_##format, component_swizzle, vector_swizzle }

// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkFormat.html
const TextureConfig texture_configs[64] = {
    /* k_1_REVERSE                 */ ___(UNDEFINED),
    /* k_1                         */ ___(UNDEFINED),
    /* k_8                         */ ___(R8_UNORM),
    /* k_1_5_5_5                   */ __v(A1R5G5B5_UNORM_PACK16, ZYXW),
    /* k_5_6_5                     */ __v(R5G6B5_UNORM_PACK16, ZYXW),
    /* k_6_5_5                     */ ___(UNDEFINED),
    /* k_8_8_8_8                   */ ___(R8G8B8A8_UNORM),
    /* k_2_10_10_10                */ ___(A2R10G10B10_UNORM_PACK32),
    /* k_8_A                       */ ___(R8_UNORM),
    /* k_8_B                       */ ___(UNDEFINED),
    /* k_8_8                       */ ___(R8G8_UNORM),
    /* k_Cr_Y1_Cb_Y0_REP           */ ___(UNDEFINED),
    /* k_Y1_Cr_Y0_Cb_REP           */ ___(UNDEFINED),
    /* k_16_16_EDRAM               */ ___(UNDEFINED),
    /* k_8_8_8_8_A                 */ ___(UNDEFINED),
    /* k_4_4_4_4                   */ __v(R4G4B4A4_UNORM_PACK16, YXWZ),
    // TODO: Verify if these two are correct (I think not).
    /* k_10_11_11                  */ ___(B10G11R11_UFLOAT_PACK32),
    /* k_11_11_10                  */ ___(B10G11R11_UFLOAT_PACK32),

    /* k_DXT1                      */ ___(BC1_RGBA_UNORM_BLOCK),
    /* k_DXT2_3                    */ ___(BC2_UNORM_BLOCK),
    /* k_DXT4_5                    */ ___(BC3_UNORM_BLOCK),
    /* k_16_16_16_16_EDRAM         */ ___(UNDEFINED),

    // TODO: D24 unsupported on AMD.
    /* k_24_8                      */ ___(D24_UNORM_S8_UINT),
    /* k_24_8_FLOAT                */ ___(D32_SFLOAT_S8_UINT),
    /* k_16                        */ ___(R16_UNORM),
    /* k_16_16                     */ ___(R16G16_UNORM),
    /* k_16_16_16_16               */ ___(R16G16B16A16_UNORM),
    /* k_16_EXPAND                 */ ___(R16_SFLOAT),
    /* k_16_16_EXPAND              */ ___(R16G16_SFLOAT),
    /* k_16_16_16_16_EXPAND        */ ___(R16G16B16A16_SFLOAT),
    /* k_16_FLOAT                  */ ___(R16_SFLOAT),
    /* k_16_16_FLOAT               */ ___(R16G16_SFLOAT),
    /* k_16_16_16_16_FLOAT         */ ___(R16G16B16A16_SFLOAT),

    // ! These are UNORM formats, not SINT.
    /* k_32                        */ ___(R32_SINT),
    /* k_32_32                     */ ___(R32G32_SINT),
    /* k_32_32_32_32               */ ___(R32G32B32A32_SINT),
    /* k_32_FLOAT                  */ ___(R32_SFLOAT),
    /* k_32_32_FLOAT               */ ___(R32G32_SFLOAT),
    /* k_32_32_32_32_FLOAT         */ ___(R32G32B32A32_SFLOAT),
    /* k_32_AS_8                   */ ___(UNDEFINED),
    /* k_32_AS_8_8                 */ ___(UNDEFINED),
    /* k_16_MPEG                   */ ___(UNDEFINED),
    /* k_16_16_MPEG                */ ___(UNDEFINED),
    /* k_8_INTERLACED              */ ___(UNDEFINED),
    /* k_32_AS_8_INTERLACED        */ ___(UNDEFINED),
    /* k_32_AS_8_8_INTERLACED      */ ___(UNDEFINED),
    /* k_16_INTERLACED             */ ___(UNDEFINED),
    /* k_16_MPEG_INTERLACED        */ ___(UNDEFINED),
    /* k_16_16_MPEG_INTERLACED     */ ___(UNDEFINED),

    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    /* k_DXN                       */ ___(BC5_UNORM_BLOCK),  // ?

    /* k_8_8_8_8_AS_16_16_16_16    */ ___(R8G8B8A8_UNORM),
    /* k_DXT1_AS_16_16_16_16       */ ___(BC1_RGBA_UNORM_BLOCK),
    /* k_DXT2_3_AS_16_16_16_16     */ ___(BC2_UNORM_BLOCK),
    /* k_DXT4_5_AS_16_16_16_16     */ ___(BC3_UNORM_BLOCK),

    /* k_2_10_10_10_AS_16_16_16_16 */ ___(A2R10G10B10_UNORM_PACK32),

    // TODO: Verify if these two are correct (I think not).
    /* k_10_11_11_AS_16_16_16_16   */ ___(B10G11R11_UFLOAT_PACK32),  // ?
    /* k_11_11_10_AS_16_16_16_16   */ ___(B10G11R11_UFLOAT_PACK32),  // ?
    /* k_32_32_32_FLOAT            */ ___(R32G32B32_SFLOAT),
    /* k_DXT3A                     */ _c_(BC2_UNORM_BLOCK, ___R),
    /* k_DXT5A                     */ _c_(BC4_UNORM_BLOCK, RRRR),  // ATI1N

    // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
    /* k_CTX1                      */ ___(R8G8_UINT),

    /* k_DXT3A_AS_1_1_1_1          */ ___(UNDEFINED),

    /* k_8_8_8_8_GAMMA_EDRAM       */ ___(UNDEFINED),
    /* k_2_10_10_10_FLOAT_EDRAM    */ ___(UNDEFINED),
};

#undef _cv
#undef __v
#undef _c_
#undef ___

#undef ZYXW
#undef YXWZ
#undef XYZW

#undef RRRR
#undef ___R
#undef RGBA

#undef VEC_SWIZ
#undef COMP_SWIZ

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
