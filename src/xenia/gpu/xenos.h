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

#if XE_COMPILER_MSVC
#define XEPACKEDSTRUCT(name, value)                                  \
  __pragma(pack(push, 1)) struct name##_s value __pragma(pack(pop)); \
  typedef struct name##_s name;
#define XEPACKEDSTRUCTANONYMOUS(value) \
  __pragma(pack(push, 1)) struct value __pragma(pack(pop));
#define XEPACKEDUNION(name, value)                                  \
  __pragma(pack(push, 1)) union name##_s value __pragma(pack(pop)); \
  typedef union name##_s name;
#else
#define XEPACKEDSTRUCT(name, value) struct __attribute__((packed)) name value;
#define XEPACKEDSTRUCTANONYMOUS(value) struct __attribute__((packed)) value;
#define XEPACKEDUNION(name, value) union __attribute__((packed)) name value;
#endif  // XE_PLATFORM_WIN32

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
  kUnknown0x07 = 0x07,
  kRectangleList = 0x08,
  kLineLoop = 0x0C,
  kQuadList = 0x0D,
  kQuadStrip = 0x0E,
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

enum class MsaaSamples : uint32_t {
  k1X = 0,
  k2X = 1,
  k4X = 2,
};

enum class ColorRenderTargetFormat : uint32_t {
  k_8_8_8_8 = 0,        // D3DFMT_A8R8G8B8 (or ABGR?)
  k_8_8_8_8_GAMMA = 1,  // D3DFMT_A8R8G8B8 with gamma correction
  k_2_10_10_10 = 2,
  k_2_10_10_10_FLOAT = 3,
  k_16_16 = 4,
  k_16_16_16_16 = 5,
  k_16_16_FLOAT = 6,
  k_16_16_16_16_FLOAT = 7,
  k_2_10_10_10_unknown = 10,
  k_2_10_10_10_FLOAT_unknown = 12,
  k_32_FLOAT = 14,
  k_32_32_FLOAT = 15,
};

enum class DepthRenderTargetFormat : uint32_t {
  kD24S8 = 0,
  kD24FS8 = 1,
};

// Subset of a2xx_sq_surfaceformat.
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
  k_2_10_10_10_FLOAT = 62,

  kUnknown0x36 = 0x36,  // not sure, but like 8888
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
    uint32_t unknown0 : 1;
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
    uint32_t type : 2;
    uint32_t address : 30;
    uint32_t endian : 2;
    uint32_t size : 24;
    uint32_t unk1 : 6;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
  });
});

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_texture_fetch_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type : 2;  // dword_0
    uint32_t sign_x : 2;
    uint32_t sign_y : 2;
    uint32_t sign_z : 2;
    uint32_t sign_w : 2;
    uint32_t clamp_x : 3;
    uint32_t clamp_y : 3;
    uint32_t clamp_z : 3;
    uint32_t unk0 : 3;
    uint32_t pitch : 9;
    uint32_t tiled : 1;
    uint32_t format : 6;  // dword_1
    uint32_t endianness : 2;
    uint32_t unk1 : 4;
    uint32_t address : 20;
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
    uint32_t unk3_0 : 1;    // dword_3
    uint32_t swizzle : 12;  // xyzw, 3b each (XE_GPU_SWIZZLE)
    uint32_t unk3_1 : 6;
    uint32_t mag_filter : 2;
    uint32_t min_filter : 2;
    uint32_t mip_filter : 2;
    uint32_t aniso_filter : 3;
    uint32_t unk3_2 : 3;
    uint32_t border : 1;
    uint32_t unk4_0 : 2;  // dword_4
    uint32_t mip_min_level : 4;
    uint32_t mip_max_level : 4;
    uint32_t unk4_1 : 22;
    uint32_t unk5 : 9;  // dword_5
    uint32_t dimension : 2;
    uint32_t unk5b : 21;
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
  PM4_WAT_REG_GTE           = 0x53,   // wait until a register location is >= a specific value
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
  PM4_SET_CONSTANT2         = 0x55,
  PM4_LOAD_ALU_CONSTANT     = 0x2f,   // load constants from memory
  PM4_SET_SHADER_CONSTANTS  = 0x56,   // ?? constant values
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

template <uint16_t index, uint16_t count, bool one_reg = false>
constexpr inline uint32_t MakePacketType0() {
  // ttcccccc cccccccc oiiiiiii iiiiiiii
  static_assert(index <= 0x7FFF, "index must be <= 0x7FFF");
  static_assert(count >= 1 && count <= 0x4000,
                "count must be >= 1 and <= 0x4000");
  return (0u << 30) | (((count - 1) & 0x3FFF) << 16) | (index & 0x7FFF);
}

template <uint16_t index_1, uint16_t index_2>
constexpr inline uint32_t MakePacketType1() {
  // tt?????? ??222222 22222111 11111111
  static_assert(index_1 <= 0x7FF, "index_1 must be <= 0x7FF");
  static_assert(index_2 <= 0x7FF, "index_2 must be <= 0x7FF");
  return (1u << 30) | ((index_2 & 0x7FF) << 11) | (index_1 & 0x7FF);
}

constexpr inline uint32_t MakePacketType2() {
  // tt?????? ???????? ???????? ????????
  return (2u << 30);
}

template <Type3Opcode opcode, uint16_t count, bool predicate = false>
constexpr inline uint32_t MakePacketType3() {
  // ttcccccc cccccccc ?ooooooo ???????p
  static_assert(opcode <= 0x7F, "opcode must be <= 0x7F");
  static_assert(count >= 1 && count <= 0x4000,
                "count must be >= 1 and <= 0x4000");
  return (3u << 30) | (((count - 1) & 0x3FFF) << 16) | ((opcode & 0x7F) << 8) |
         (predicate ? 1 : 0);
}

}  // namespace xenos
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_XENOS_H_
