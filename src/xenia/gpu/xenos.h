/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_XENOS_H_
#define XENIA_GPU_XENOS_H_

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/platform.h"

namespace xe {
namespace gpu {

enum class ShaderType : uint32_t {
  kVertex = 0,
  kPixel = 1,
};

enum class PrimitiveType : uint32_t {
  kNone = 0x00,
  kPointList = 0x01,
  kLineList = 0x02,
  kLineStrip = 0x03,
  kTriangleList = 0x04,
  kTriangleFan = 0x05,
  kTriangleStrip = 0x06,
  kTriangleWithWFlags = 0x07,
  kRectangleList = 0x08,
  kLineLoop = 0x0C,
  kQuadList = 0x0D,
  kQuadStrip = 0x0E,
  kPolygon = 0x0F,
  k2DCopyRectListV0 = 0x10,
  k2DCopyRectListV1 = 0x11,
  k2DCopyRectListV2 = 0x12,
  k2DCopyRectListV3 = 0x13,
  k2DFillRectList = 0x14,
  k2DLineStrip = 0x15,
  k2DTriStrip = 0x16,
  // Tessellation patches (D3DTPT) - reusing 2DCopyRectList types.
  kLinePatch = 0x10,
  kTrianglePatch = 0x11,
  kQuadPatch = 0x12,
};

enum class TessellationMode : uint32_t {
  kDiscrete = 0,
  kContinuous = 1,
  kAdaptive = 2,
};

enum class Dimension : uint32_t {
  k1D = 0,
  k2D = 1,
  k3D = 2,
  kCube = 3,
};

enum class ClampMode : uint32_t {
  kRepeat = 0,
  kMirroredRepeat = 1,
  kClampToEdge = 2,
  kMirrorClampToEdge = 3,
  kClampToHalfway = 4,
  kMirrorClampToHalfway = 5,
  kClampToBorder = 6,
  kMirrorClampToBorder = 7,
};

// TEX_FORMAT_COMP, known as GPUSIGN on the Xbox 360.
enum class TextureSign : uint32_t {
  kUnsigned = 0,
  // Two's complement texture data.
  kSigned = 1,
  // 2*color-1 - https://xboxforums.create.msdn.com/forums/t/107374.aspx
  kUnsignedBiased = 2,
  // Linearized when sampled.
  kGamma = 3,
};

enum class TextureFilter : uint32_t {
  kPoint = 0,
  kLinear = 1,
  kBaseMap = 2,  // Only applicable for mip-filter.
  kUseFetchConst = 3,
};

enum class AnisoFilter : uint32_t {
  kDisabled = 0,
  kMax_1_1 = 1,
  kMax_2_1 = 2,
  kMax_4_1 = 3,
  kMax_8_1 = 4,
  kMax_16_1 = 5,
  kUseFetchConst = 7,
};

enum class BorderColor : uint32_t {
  k_AGBR_Black = 0,
  k_AGBR_White = 1,
  k_ACBYCR_BLACK = 2,
  k_ACBCRY_BLACK = 3,
};

enum class TextureDimension : uint32_t {
  k1D = 0,
  k2D = 1,
  k3D = 2,
  kCube = 3,
};

inline int GetTextureDimensionComponentCount(TextureDimension dimension) {
  switch (dimension) {
    case TextureDimension::k1D:
      return 1;
    case TextureDimension::k2D:
      return 2;
    case TextureDimension::k3D:
    case TextureDimension::kCube:
      return 3;
    default:
      assert_unhandled_case(dimension);
      return 1;
  }
}

enum class SampleLocation : uint32_t {
  kCentroid = 0,
  kCenter = 1,
};

enum class Endian : uint32_t {
  kUnspecified = 0,
  k8in16 = 1,
  k8in32 = 2,
  k16in32 = 3,
};

enum class Endian128 : uint32_t {
  kUnspecified = 0,
  k8in16 = 1,
  k8in32 = 2,
  k16in32 = 3,
  k8in64 = 4,
  k8in128 = 5,
};

enum class IndexFormat : uint32_t {
  kInt16,
  kInt32,
};

// GPUSURFACENUMBER from a game .pdb. "Repeat" means repeating fraction, it's
// what ATI calls normalized.
enum class SurfaceNumFormat : uint32_t {
  kUnsignedRepeat = 0,
  kSignedRepeat = 1,
  kUnsignedInteger = 2,
  kSignedInteger = 3,
  kFloat = 7,
};

enum class MsaaSamples : uint32_t {
  k1X = 0,
  k2X = 1,
  k4X = 2,
};

enum class ColorRenderTargetFormat : uint32_t {
  // D3DFMT_A8R8G8B8 (or ABGR?).
  k_8_8_8_8 = 0,
  // D3DFMT_A8R8G8B8 with gamma correction.
  k_8_8_8_8_GAMMA = 1,
  k_2_10_10_10 = 2,
  // 7e3 [0, 32) RGB, unorm alpha.
  // http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/eg05-xenos-doggett.pdf
  k_2_10_10_10_FLOAT = 3,
  // Fixed point -32...32.
  // http://www.students.science.uu.nl/~3220516/advancedgraphics/papers/inferred_lighting.pdf
  k_16_16 = 4,
  // Fixed point -32...32.
  k_16_16_16_16 = 5,
  k_16_16_FLOAT = 6,
  k_16_16_16_16_FLOAT = 7,
  k_2_10_10_10_AS_10_10_10_10 = 10,
  k_2_10_10_10_FLOAT_AS_16_16_16_16 = 12,
  k_32_FLOAT = 14,
  k_32_32_FLOAT = 15,
};

enum class DepthRenderTargetFormat : uint32_t {
  kD24S8 = 0,
  // 20e4 [0, 2).
  kD24FS8 = 1,
};

// Subset of a2xx_sq_surfaceformat - formats that RTs can be resolved to.
enum class ColorFormat : uint32_t {
  k_8 = 2,
  k_1_5_5_5 = 3,
  k_5_6_5 = 4,
  k_6_5_5 = 5,
  k_8_8_8_8 = 6,
  k_2_10_10_10 = 7,
  k_8_A = 8,
  k_8_B = 9,
  k_8_8 = 10,
  k_8_8_8_8_A = 14,
  k_4_4_4_4 = 15,
  k_10_11_11 = 16,
  k_11_11_10 = 17,
  k_16 = 24,
  k_16_16 = 25,
  k_16_16_16_16 = 26,
  k_16_FLOAT = 30,
  k_16_16_FLOAT = 31,
  k_16_16_16_16_FLOAT = 32,
  k_32_FLOAT = 36,
  k_32_32_FLOAT = 37,
  k_32_32_32_32_FLOAT = 38,
  k_8_8_8_8_AS_16_16_16_16 = 50,
  k_2_10_10_10_AS_16_16_16_16 = 54,
  k_10_11_11_AS_16_16_16_16 = 55,
  k_11_11_10_AS_16_16_16_16 = 56,
};

enum class VertexFormat : uint32_t {
  kUndefined = 0,
  k_8_8_8_8 = 6,
  k_2_10_10_10 = 7,
  k_10_11_11 = 16,
  k_11_11_10 = 17,
  k_16_16 = 25,
  k_16_16_16_16 = 26,
  k_16_16_FLOAT = 31,
  k_16_16_16_16_FLOAT = 32,
  k_32 = 33,
  k_32_32 = 34,
  k_32_32_32_32 = 35,
  k_32_FLOAT = 36,
  k_32_32_FLOAT = 37,
  k_32_32_32_32_FLOAT = 38,
  k_32_32_32_FLOAT = 57,
};

inline int GetVertexFormatComponentCount(VertexFormat format) {
  switch (format) {
    case VertexFormat::k_32:
    case VertexFormat::k_32_FLOAT:
      return 1;
    case VertexFormat::k_16_16:
    case VertexFormat::k_16_16_FLOAT:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      return 2;
    case VertexFormat::k_10_11_11:
    case VertexFormat::k_11_11_10:
    case VertexFormat::k_32_32_32_FLOAT:
      return 3;
    case VertexFormat::k_8_8_8_8:
    case VertexFormat::k_2_10_10_10:
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      return 4;
    default:
      assert_unhandled_case(format);
      return 0;
  }
}

inline int GetVertexFormatSizeInWords(VertexFormat format) {
  switch (format) {
    case VertexFormat::k_8_8_8_8:
    case VertexFormat::k_2_10_10_10:
    case VertexFormat::k_10_11_11:
    case VertexFormat::k_11_11_10:
    case VertexFormat::k_16_16:
    case VertexFormat::k_16_16_FLOAT:
    case VertexFormat::k_32:
    case VertexFormat::k_32_FLOAT:
      return 1;
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      return 2;
    case VertexFormat::k_32_32_32_FLOAT:
      return 3;
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      return 4;
    default:
      assert_unhandled_case(format);
      return 1;
  }
}

// adreno_rb_blend_factor
enum class BlendFactor : uint32_t {
  kZero = 0,
  kOne = 1,
  kSrcColor = 4,
  kOneMinusSrcColor = 5,
  kSrcAlpha = 6,
  kOneMinusSrcAlpha = 7,
  kDstColor = 8,
  kOneMinusDstColor = 9,
  kDstAlpha = 10,
  kOneMinusDstAlpha = 11,
  kConstantColor = 12,
  kOneMinusConstantColor = 13,
  kConstantAlpha = 14,
  kOneMinusConstantAlpha = 15,
  kSrcAlphaSaturate = 16,
  // SRC1 likely not used on the Xbox 360 - only available in Direct3D 9Ex.
  kSrc1Color = 20,
  kOneMinusSrc1Color = 21,
  kSrc1Alpha = 22,
  kOneMinusSrc1Alpha = 23,
};

enum class BlendOp : uint32_t {
  kAdd = 0,
  kSubtract = 1,
  kMin = 2,
  kMax = 3,
  kRevSubtract = 4,
};

namespace xenos {

typedef enum {
  XE_GPU_INVALIDATE_MASK_VERTEX_SHADER = 1 << 8,
  XE_GPU_INVALIDATE_MASK_PIXEL_SHADER = 1 << 9,

  XE_GPU_INVALIDATE_MASK_ALL = 0x7FFF,
} XE_GPU_INVALIDATE_MASK;

enum class ModeControl : uint32_t {
  kIgnore = 0,
  kColorDepth = 4,
  kDepth = 5,
  kCopy = 6,
};

enum class CopyCommand : uint32_t {
  kRaw = 0,
  kConvert = 1,
  kConstantOne = 2,
  kNull = 3,  // ?
};

// a2xx_rb_copy_sample_select
enum class CopySampleSelect : uint32_t {
  k0,
  k1,
  k2,
  k3,
  k01,
  k23,
  k0123,
};

#define XE_GPU_MAKE_SWIZZLE(x, y, z, w)                        \
  (((XE_GPU_SWIZZLE_##x) << 0) | ((XE_GPU_SWIZZLE_##y) << 3) | \
   ((XE_GPU_SWIZZLE_##z) << 6) | ((XE_GPU_SWIZZLE_##w) << 9))
typedef enum {
  XE_GPU_SWIZZLE_X = 0,
  XE_GPU_SWIZZLE_R = 0,
  XE_GPU_SWIZZLE_Y = 1,
  XE_GPU_SWIZZLE_G = 1,
  XE_GPU_SWIZZLE_Z = 2,
  XE_GPU_SWIZZLE_B = 2,
  XE_GPU_SWIZZLE_W = 3,
  XE_GPU_SWIZZLE_A = 3,
  XE_GPU_SWIZZLE_0 = 4,
  XE_GPU_SWIZZLE_1 = 5,
  XE_GPU_SWIZZLE_RGBA = XE_GPU_MAKE_SWIZZLE(R, G, B, A),
  XE_GPU_SWIZZLE_BGRA = XE_GPU_MAKE_SWIZZLE(B, G, R, A),
  XE_GPU_SWIZZLE_RGB1 = XE_GPU_MAKE_SWIZZLE(R, G, B, 1),
  XE_GPU_SWIZZLE_BGR1 = XE_GPU_MAKE_SWIZZLE(B, G, R, 1),
  XE_GPU_SWIZZLE_000R = XE_GPU_MAKE_SWIZZLE(0, 0, 0, R),
  XE_GPU_SWIZZLE_RRR1 = XE_GPU_MAKE_SWIZZLE(R, R, R, 1),
  XE_GPU_SWIZZLE_R111 = XE_GPU_MAKE_SWIZZLE(R, 1, 1, 1),
  XE_GPU_SWIZZLE_R000 = XE_GPU_MAKE_SWIZZLE(R, 0, 0, 0),
} XE_GPU_SWIZZLE;

inline uint16_t GpuSwap(uint16_t value, Endian endianness) {
  switch (endianness) {
    case Endian::kUnspecified:
      // No swap.
      return value;
    case Endian::k8in16:
      // Swap bytes in half words.
      return ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0x00FF00FF);
    default:
      assert_unhandled_case(endianness);
      return value;
  }
}

inline uint32_t GpuSwap(uint32_t value, Endian endianness) {
  switch (endianness) {
    default:
    case Endian::kUnspecified:
      // No swap.
      return value;
    case Endian::k8in16:
      // Swap bytes in half words.
      return ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0x00FF00FF);
    case Endian::k8in32:
      // Swap bytes.
      // NOTE: we are likely doing two swaps here. Wasteful. Oh well.
      return xe::byte_swap(value);
    case Endian::k16in32:
      // Swap half words.
      return ((value >> 16) & 0xFFFF) | (value << 16);
  }
}

inline float GpuSwap(float value, Endian endianness) {
  union {
    uint32_t i;
    float f;
  } v;
  v.f = value;
  v.i = GpuSwap(v.i, endianness);
  return v.f;
}

inline uint32_t GpuToCpu(uint32_t p) { return p; }

inline uint32_t CpuToGpu(uint32_t p) { return p & 0x1FFFFFFF; }

// XE_GPU_REG_SQ_PROGRAM_CNTL
typedef union {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t vs_regs : 6;
    uint32_t unk_0 : 2;
    uint32_t ps_regs : 6;
    uint32_t unk_1 : 2;
    uint32_t vs_resource : 1;
    uint32_t ps_resource : 1;
    uint32_t param_gen : 1;
    uint32_t gen_index_pix : 1;
    uint32_t vs_export_count : 4;
    uint32_t vs_export_mode : 3;
    uint32_t ps_export_depth : 1;
    uint32_t ps_export_count : 3;
    uint32_t gen_index_vtx : 1;
  });
  XEPACKEDSTRUCTANONYMOUS({ uint32_t dword_0; });
} xe_gpu_program_cntl_t;

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_vertex_fetch_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type : 2;      // +0
    uint32_t address : 30;  // +2

    uint32_t endian : 2;  // +0
    uint32_t size : 24;   // +2 size in words
    uint32_t unk1 : 6;    // +26
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
  });
});

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_texture_fetch_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type : 2;      // +0 dword_0
    uint32_t sign_x : 2;    // +2
    uint32_t sign_y : 2;    // +4
    uint32_t sign_z : 2;    // +6
    uint32_t sign_w : 2;    // +8
    uint32_t clamp_x : 3;   // +10
    uint32_t clamp_y : 3;   // +13
    uint32_t clamp_z : 3;   // +16
    uint32_t unused_0 : 3;  // +19
    uint32_t pitch : 9;     // +22 byte_pitch >> 5
    uint32_t tiled : 1;     // +31

    uint32_t format : 6;         // +0 dword_1
    uint32_t endianness : 2;     // +6
    uint32_t request_size : 2;   // +8
    uint32_t stacked : 1;        // +10
    uint32_t clamp_policy : 1;   // +11 d3d/opengl
    uint32_t base_address : 20;  // +12

    union {  // dword_2
      struct {
        uint32_t width : 24;
        uint32_t unused : 8;
      } size_1d;
      struct {
        uint32_t width : 13;
        uint32_t height : 13;
        uint32_t unused : 6;
      } size_2d;
      struct {
        uint32_t width : 13;
        uint32_t height : 13;
        uint32_t depth : 6;
      } size_stack;
      struct {
        uint32_t width : 11;
        uint32_t height : 11;
        uint32_t depth : 10;
      } size_3d;
    };

    uint32_t num_format : 1;    // +0 dword_3 frac/int
    uint32_t swizzle : 12;      // +1 xyzw, 3b each (XE_GPU_SWIZZLE)
    int32_t exp_adjust : 6;     // +13
    uint32_t mag_filter : 2;    // +19
    uint32_t min_filter : 2;    // +21
    uint32_t mip_filter : 2;    // +23
    uint32_t aniso_filter : 3;  // +25
    uint32_t unused_3 : 3;      // +28
    uint32_t border_size : 1;   // +31

    uint32_t vol_mag_filter : 1;    // +0 dword_4
    uint32_t vol_min_filter : 1;    // +1
    uint32_t mip_min_level : 4;     // +2
    uint32_t mip_max_level : 4;     // +6
    uint32_t mag_aniso_walk : 1;    // +10
    uint32_t min_aniso_walk : 1;    // +11
    int32_t lod_bias : 10;          // +12
    int32_t grad_exp_adjust_h : 5;  // +22
    int32_t grad_exp_adjust_v : 5;  // +27

    uint32_t border_color : 2;   // +0 dword_5
    uint32_t force_bcw_max : 1;  // +2
    uint32_t tri_clamp : 2;      // +3
    int32_t aniso_bias : 4;      // +5
    uint32_t dimension : 2;      // +9
    uint32_t packed_mips : 1;    // +11
    uint32_t mip_address : 20;   // +12
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
    uint32_t dword_2;
    uint32_t dword_3;
    uint32_t dword_4;
    uint32_t dword_5;
  });
});

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_fetch_group_t, {
  xe_gpu_texture_fetch_t texture_fetch;
  XEPACKEDSTRUCTANONYMOUS({
    xe_gpu_vertex_fetch_t vertex_fetch_0;
    xe_gpu_vertex_fetch_t vertex_fetch_1;
    xe_gpu_vertex_fetch_t vertex_fetch_2;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
    uint32_t dword_2;
    uint32_t dword_3;
    uint32_t dword_4;
    uint32_t dword_5;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type_0 : 2;
    uint32_t data_0_a : 30;
    uint32_t data_0_b : 32;
    uint32_t type_1 : 2;
    uint32_t data_1_a : 30;
    uint32_t data_1_b : 32;
    uint32_t type_2 : 2;
    uint32_t data_2_a : 30;
    uint32_t data_2_b : 32;
  });
});

// GPU_MEMEXPORT_STREAM_CONSTANT from a game .pdb - float constant for memexport
// stream configuration.
// This is used with the floating-point ALU in shaders (written to eA using
// mad), so the dwords have a normalized exponent when reinterpreted as floats
// (otherwise they would be flushed to zero), but actually these are packed
// integers. dword_1 specifically is 2^23 because
// powf(2.0f, 23.0f) + float(i) == 0x4B000000 | i
// so mad can pack indices as integers in the lower bits.
XEPACKEDUNION(xe_gpu_memexport_stream_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t base_address : 30;  // +0 dword_0 physical address >> 2
    uint32_t const_0x1 : 2;      // +30

    uint32_t const_0x4b000000;  // +0 dword_1

    Endian128 endianness : 3;         // +0 dword_2
    uint32_t unused_0 : 5;            // +3
    ColorFormat format : 6;           // +8
    uint32_t unused_1 : 2;            // +14
    SurfaceNumFormat num_format : 3;  // +16
    uint32_t red_blue_swap : 1;       // +19
    uint32_t const_0x4b0 : 12;        // +20

    uint32_t index_count : 23;  // +0 dword_3
    uint32_t const_0x96 : 9;    // +23
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
    uint32_t dword_2;
    uint32_t dword_3;
  });
});

// Enum of event values used for VGT_EVENT_INITIATOR
enum Event {
  VS_DEALLOC = 0,
  PS_DEALLOC = 1,
  VS_DONE_TS = 2,
  PS_DONE_TS = 3,
  CACHE_FLUSH_TS = 4,
  CONTEXT_DONE = 5,
  CACHE_FLUSH = 6,
  VIZQUERY_START = 7,
  VIZQUERY_END = 8,
  SC_WAIT_WC = 9,
  MPASS_PS_CP_REFETCH = 10,
  MPASS_PS_RST_START = 11,
  MPASS_PS_INCR_START = 12,
  RST_PIX_CNT = 13,
  RST_VTX_CNT = 14,
  TILE_FLUSH = 15,
  CACHE_FLUSH_AND_INV_TS_EVENT = 20,
  ZPASS_DONE = 21,
  CACHE_FLUSH_AND_INV_EVENT = 22,
  PERFCOUNTER_START = 23,
  PERFCOUNTER_STOP = 24,
  SCREEN_EXT_INIT = 25,
  SCREEN_EXT_RPT = 26,
  VS_FETCH_DONE_TS = 27,
};

// Opcodes (IT_OPCODE) for Type-3 commands in the ringbuffer.
// https://github.com/freedreno/amd-gpu/blob/master/include/api/gsl_pm4types.h
// Not sure if all of these are used.
// clang-format off
enum Type3Opcode {
  PM4_ME_INIT               = 0x48,   // initialize CP's micro-engine

  PM4_NOP                   = 0x10,   // skip N 32-bit words to get to the next packet

  PM4_INDIRECT_BUFFER       = 0x3f,   // indirect buffer dispatch.  prefetch parser uses this packet type to determine whether to pre-fetch the IB
  PM4_INDIRECT_BUFFER_PFD   = 0x37,   // indirect buffer dispatch.  same as IB, but init is pipelined

  PM4_WAIT_FOR_IDLE         = 0x26,   // wait for the IDLE state of the engine
  PM4_WAIT_REG_MEM          = 0x3c,   // wait until a register or memory location is a specific value
  PM4_WAIT_REG_EQ           = 0x52,   // wait until a register location is equal to a specific value
  PM4_WAIT_REG_GTE          = 0x53,   // wait until a register location is >= a specific value
  PM4_WAIT_UNTIL_READ       = 0x5c,   // wait until a read completes
  PM4_WAIT_IB_PFD_COMPLETE  = 0x5d,   // wait until all base/size writes from an IB_PFD packet have completed

  PM4_REG_RMW               = 0x21,   // register read/modify/write
  PM4_REG_TO_MEM            = 0x3e,   // reads register in chip and writes to memory
  PM4_MEM_WRITE             = 0x3d,   // write N 32-bit words to memory
  PM4_MEM_WRITE_CNTR        = 0x4f,   // write CP_PROG_COUNTER value to memory
  PM4_COND_EXEC             = 0x44,   // conditional execution of a sequence of packets
  PM4_COND_WRITE            = 0x45,   // conditional write to memory or register

  PM4_EVENT_WRITE           = 0x46,   // generate an event that creates a write to memory when completed
  PM4_EVENT_WRITE_SHD       = 0x58,   // generate a VS|PS_done event
  PM4_EVENT_WRITE_CFL       = 0x59,   // generate a cache flush done event
  PM4_EVENT_WRITE_EXT       = 0x5a,   // generate a screen extent event
  PM4_EVENT_WRITE_ZPD       = 0x5b,   // generate a z_pass done event

  PM4_DRAW_INDX             = 0x22,   // initiate fetch of index buffer and draw
  PM4_DRAW_INDX_2           = 0x36,   // draw using supplied indices in packet
  PM4_DRAW_INDX_BIN         = 0x34,   // initiate fetch of index buffer and binIDs and draw
  PM4_DRAW_INDX_2_BIN       = 0x35,   // initiate fetch of bin IDs and draw using supplied indices

  PM4_VIZ_QUERY             = 0x23,   // begin/end initiator for viz query extent processing
  PM4_SET_STATE             = 0x25,   // fetch state sub-blocks and initiate shader code DMAs
  PM4_SET_CONSTANT          = 0x2d,   // load constant into chip and to memory
  PM4_SET_CONSTANT2         = 0x55,   // INCR_UPDATE_STATE
  PM4_SET_SHADER_CONSTANTS  = 0x56,   // INCR_UPDT_CONST
  PM4_LOAD_ALU_CONSTANT     = 0x2f,   // load constants from memory
  PM4_IM_LOAD               = 0x27,   // load sequencer instruction memory (pointer-based)
  PM4_IM_LOAD_IMMEDIATE     = 0x2b,   // load sequencer instruction memory (code embedded in packet)
  PM4_LOAD_CONSTANT_CONTEXT = 0x2e,   // load constants from a location in memory
  PM4_INVALIDATE_STATE      = 0x3b,   // selective invalidation of state pointers

  PM4_SET_SHADER_BASES      = 0x4A,   // dynamically changes shader instruction memory partition
  PM4_SET_BIN_BASE_OFFSET   = 0x4B,   // program an offset that will added to the BIN_BASE value of the 3D_DRAW_INDX_BIN packet
  PM4_SET_BIN_MASK          = 0x50,   // sets the 64-bit BIN_MASK register in the PFP
  PM4_SET_BIN_SELECT        = 0x51,   // sets the 64-bit BIN_SELECT register in the PFP

  PM4_CONTEXT_UPDATE        = 0x5e,   // updates the current context, if needed
  PM4_INTERRUPT             = 0x54,   // generate interrupt from the command stream

  PM4_XE_SWAP               = 0x64,   // Xenia only: VdSwap uses this to trigger a swap.

  PM4_IM_STORE              = 0x2c,   // copy sequencer instruction memory to system memory

  // Tiled rendering:
  // https://www.google.com/patents/US20060055701
  PM4_SET_BIN_MASK_LO       = 0x60,
  PM4_SET_BIN_MASK_HI       = 0x61,
  PM4_SET_BIN_SELECT_LO     = 0x62,
  PM4_SET_BIN_SELECT_HI     = 0x63,
};
// clang-format on

inline uint32_t MakePacketType0(uint16_t index, uint16_t count,
                                bool one_reg = false) {
  // ttcccccc cccccccc oiiiiiii iiiiiiii
  assert(index <= 0x7FFF);
  assert(count >= 1 && count <= 0x4000);
  return (0u << 30) | (((count - 1) & 0x3FFF) << 16) | (index & 0x7FFF);
}

inline uint32_t MakePacketType1(uint16_t index_1, uint16_t index_2) {
  // tt?????? ??222222 22222111 11111111
  assert(index_1 <= 0x7FF);
  assert(index_2 <= 0x7FF);
  return (1u << 30) | ((index_2 & 0x7FF) << 11) | (index_1 & 0x7FF);
}

constexpr inline uint32_t MakePacketType2() {
  // tt?????? ???????? ???????? ????????
  return (2u << 30);
}

inline uint32_t MakePacketType3(Type3Opcode opcode, uint16_t count,
                                bool predicate = false) {
  // ttcccccc cccccccc ?ooooooo ???????p
  assert(opcode <= 0x7F);
  assert(count >= 1 && count <= 0x4000);
  return (3u << 30) | (((count - 1) & 0x3FFF) << 16) | ((opcode & 0x7F) << 8) |
         (predicate ? 1 : 0);
}

}  // namespace xenos
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_XENOS_H_
