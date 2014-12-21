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

#include <xenia/common.h>
#include <xenia/gpu/ucode.h>

namespace xe {
namespace gpu {
namespace xenos {

enum class ShaderType : uint32_t {
  kVertex = 0,
  kPixel = 1,
};

typedef enum {
  XE_GPU_INVALIDATE_MASK_VERTEX_SHADER    = 1 << 8,
  XE_GPU_INVALIDATE_MASK_PIXEL_SHADER     = 1 << 9,

  XE_GPU_INVALIDATE_MASK_ALL              = 0x7FFF,
} XE_GPU_INVALIDATE_MASK;

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
};

enum class Endian : uint32_t {
  kUnspecified = 0,
  k8in16 = 1,
  k8in32 = 2,
  k16in32 = 3,
};

#define XE_GPU_MAKE_SWIZZLE(x, y, z, w) \
    (((XE_GPU_SWIZZLE_##x) << 0) | ((XE_GPU_SWIZZLE_##y) << 3) | ((XE_GPU_SWIZZLE_##z) << 6) | ((XE_GPU_SWIZZLE_##w) << 9))
typedef enum {
  XE_GPU_SWIZZLE_X                        = 0,
  XE_GPU_SWIZZLE_R                        = 0,
  XE_GPU_SWIZZLE_Y                        = 1,
  XE_GPU_SWIZZLE_G                        = 1,
  XE_GPU_SWIZZLE_Z                        = 2,
  XE_GPU_SWIZZLE_B                        = 2,
  XE_GPU_SWIZZLE_W                        = 3,
  XE_GPU_SWIZZLE_A                        = 3,
  XE_GPU_SWIZZLE_0                        = 4,
  XE_GPU_SWIZZLE_1                        = 5,
  XE_GPU_SWIZZLE_RGBA                     = XE_GPU_MAKE_SWIZZLE(R, G, B, A),
  XE_GPU_SWIZZLE_BGRA                     = XE_GPU_MAKE_SWIZZLE(B, G, R, A),
  XE_GPU_SWIZZLE_RGB1                     = XE_GPU_MAKE_SWIZZLE(R, G, B, 1),
  XE_GPU_SWIZZLE_BGR1                     = XE_GPU_MAKE_SWIZZLE(B, G, R, 1),
  XE_GPU_SWIZZLE_000R                     = XE_GPU_MAKE_SWIZZLE(0, 0, 0, R),
  XE_GPU_SWIZZLE_RRR1                     = XE_GPU_MAKE_SWIZZLE(R, R, R, 1),
  XE_GPU_SWIZZLE_R111                     = XE_GPU_MAKE_SWIZZLE(R, 1, 1, 1),
  XE_GPU_SWIZZLE_R000                     = XE_GPU_MAKE_SWIZZLE(R, 0, 0, 0),
} XE_GPU_SWIZZLE;

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
      return poly::byte_swap(value);
    case Endian::k16in32:
      // Swap half words.
      return ((value >> 16) & 0xFFFF) | (value << 16);
  }
}

inline uint32_t GpuToCpu(uint32_t p) {
  return p;
}

inline uint32_t GpuToCpu(uint32_t base, uint32_t p) {
  // Some AMD docs say relative to base ptr, some say just this.
  // Some games use some crazy shift magic, but it seems to nop.
  uint32_t upper = 0;//base & 0xFF000000;
  uint32_t lower = p & 0x01FFFFFF;
  return upper + lower;// -(((base >> 20) + 0x200) & 0x1000);
}

// XE_GPU_REG_SQ_PROGRAM_CNTL
typedef union {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t vs_regs            : 6;
    uint32_t                    : 2;
    uint32_t ps_regs            : 6;
    uint32_t                    : 2;
    uint32_t vs_resource        : 1;
    uint32_t ps_resource        : 1;
    uint32_t param_gen          : 1;
    uint32_t unknown0           : 1;
    uint32_t vs_export_count    : 4;
    uint32_t vs_export_mode     : 3;
    uint32_t ps_export_depth    : 1;
    uint32_t ps_export_count    : 3;
    uint32_t gen_index_vtx      : 1;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
  });
} xe_gpu_program_cntl_t;

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_vertex_fetch_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type               : 2;
    uint32_t address            : 30;
    uint32_t endian             : 2;
    uint32_t size               : 24;
    uint32_t unk1               : 6;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
  });
});

// XE_GPU_REG_SHADER_CONSTANT_FETCH_*
XEPACKEDUNION(xe_gpu_texture_fetch_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t type               : 2;  // dword_0
    uint32_t sign_x             : 2;
    uint32_t sign_y             : 2;
    uint32_t sign_z             : 2;
    uint32_t sign_w             : 2;
    uint32_t clamp_x            : 3;
    uint32_t clamp_y            : 3;
    uint32_t clamp_z            : 3;
    uint32_t unk0               : 3;
    uint32_t pitch              : 9;
    uint32_t tiled              : 1;
    uint32_t format             : 6;  // dword_1
    uint32_t endianness         : 2;
    uint32_t unk1               : 4;
    uint32_t address            : 20;
    union {                           // dword_2
      struct {
        uint32_t width          : 24;
        uint32_t unused         : 8;
      } size_1d;
      struct {
        uint32_t width          : 13;
        uint32_t height         : 13;
        uint32_t unused         : 6;
      } size_2d;
      struct {
        uint32_t width          : 13;
        uint32_t height         : 13;
        uint32_t depth          : 6;
      } size_stack;
      struct {
        uint32_t width          : 11;
        uint32_t height         : 11;
        uint32_t depth          : 10;
      } size_3d;
    };
    uint32_t unk3_0             :  1; // dword_3
    uint32_t swizzle            :  12; // xyzw, 3b each (XE_GPU_SWIZZLE)
    uint32_t unk3_1             :  6;
    uint32_t mag_filter         :  2;
    uint32_t min_filter         :  2;
    uint32_t mip_filter         :  2;
    uint32_t unk3_2             :  6;
    uint32_t border             :  1;
    uint32_t unk4;                    // dword_4
    uint32_t unk5               : 9;  // dword_5
    uint32_t dimension          : 2;
    uint32_t unk5b              : 21;
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
    uint32_t        : 30;
    uint32_t        : 32;
    uint32_t type_1 : 2;
    uint32_t        : 30;
    uint32_t        : 32;
    uint32_t type_2 : 2;
    uint32_t        : 30;
    uint32_t        : 32;
  });
});

// Opcodes (IT_OPCODE) for Type-3 commands in the ringbuffer.
// https://github.com/freedreno/amd-gpu/blob/master/include/api/gsl_pm4types.h
// Not sure if all of these are used.
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
  PM4_EVENT_WRITE_ZPD       = 0x5b,   // generate a z_pass done event

  PM4_DRAW_INDX             = 0x22,   // initiate fetch of index buffer and draw
  PM4_DRAW_INDX_2           = 0x36,   // draw using supplied indices in packet
  PM4_DRAW_INDX_BIN         = 0x34,   // initiate fetch of index buffer and binIDs and draw
  PM4_DRAW_INDX_2_BIN       = 0x35,   // initiate fetch of bin IDs and draw using supplied indices

  PM4_VIZ_QUERY             = 0x23,   // begin/end initiator for viz query extent processing
  PM4_SET_STATE             = 0x25,   // fetch state sub-blocks and initiate shader code DMAs
  PM4_SET_CONSTANT          = 0x2d,   // load constant into chip and to memory
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

  PM4_XE_SWAP               = 0x55,   // Xenia only: VdSwap uses this to trigger a swap.

  PM4_IM_STORE              = 0x2c,   // copy sequencer instruction memory to system memory

  // Tiled rendering:
  // https://www.google.com/patents/US20060055701
  PM4_SET_BIN_MASK_LO       = 0x60,
  PM4_SET_BIN_MASK_HI       = 0x61,
  PM4_SET_BIN_SELECT_LO     = 0x62,
  PM4_SET_BIN_SELECT_HI     = 0x63,
};

}  // namespace xenos
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_XENOS_H_
