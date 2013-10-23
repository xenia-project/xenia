/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_XENOS_XENOS_H_
#define XENIA_GPU_XENOS_XENOS_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {
namespace xenos {


typedef enum {
  XE_GPU_SHADER_TYPE_VERTEX               = 0x00,
  XE_GPU_SHADER_TYPE_PIXEL                = 0x01,
} XE_GPU_SHADER_TYPE;

typedef enum {
  XE_GPU_INVALIDATE_MASK_VERTEX_SHADER    = 1 << 8,
  XE_GPU_INVALIDATE_MASK_PIXEL_SHADER     = 1 << 9,

  XE_GPU_INVALIDATE_MASK_ALL              = 0x7FFF,
} XE_GPU_INVALIDATE_MASK;

typedef enum {
  XE_GPU_PRIMITIVE_TYPE_POINT_LIST        = 0x01,
  XE_GPU_PRIMITIVE_TYPE_LINE_LIST         = 0x02,
  XE_GPU_PRIMITIVE_TYPE_LINE_STRIP        = 0x03,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_LIST     = 0x04,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_FAN      = 0x05,
  XE_GPU_PRIMITIVE_TYPE_TRIANGLE_STRIP    = 0x06,
  XE_GPU_PRIMITIVE_TYPE_UNKNOWN_07        = 0x07,
  XE_GPU_PRIMITIVE_TYPE_RECTANGLE_LIST    = 0x08,
  XE_GPU_PRIMITIVE_TYPE_LINE_LOOP         = 0x0C,
} XE_GPU_PRIMITIVE_TYPE;

typedef enum {
  XE_GPU_ENDIAN_NONE                      = 0x0,
  XE_GPU_ENDIAN_8IN16                     = 0x1,
  XE_GPU_ENDIAN_8IN32                     = 0x2,
  XE_GPU_ENDIAN_16IN32                    = 0x3,
} XE_GPU_ENDIAN;

XEFORCEINLINE uint32_t GpuSwap(uint32_t value, XE_GPU_ENDIAN endianness) {
  switch (endianness) {
  default:
  case XE_GPU_ENDIAN_NONE: // No swap.
    return value;
  case XE_GPU_ENDIAN_8IN16: // Swap bytes in half words.
    return ((value << 8) & 0xFF00FF00) |
           ((value >> 8) & 0x00FF00FF);
  case XE_GPU_ENDIAN_8IN32: // Swap bytes.
    // NOTE: we are likely doing two swaps here. Wasteful. Oh well.
    return XESWAP32(value);
  case XE_GPU_ENDIAN_16IN32: // Swap half words.
    return ((value >> 16) & 0xFFFF) | (value << 16);
  }
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
    uint32_t unk0               : 2;
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
    uint32_t unk0               : 20;
    uint32_t pitch              : 9;
    uint32_t tiled              : 1;
    uint32_t format             : 6;  // dword_1
    uint32_t endianness         : 2;
    uint32_t unk1               : 4;
    uint32_t address            : 20;
    union {                           // dword_2
      struct {
        uint32_t width          : 13;
        uint32_t height         : 13;
        uint32_t unksize2d      : 6;
      } size_2d;
    };
    uint32_t unk3;                    // dword_3
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


}  // namespace xenos
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_XENOS_XENOS_H_
