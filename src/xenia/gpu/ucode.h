/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_UCODE_H_
#define XENIA_GPU_UCODE_H_

#include <cstdint>

#include "xenia/base/platform.h"

namespace xe {
namespace gpu {

namespace ucode {

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

// Closest AMD doc:
// http://developer.amd.com/wordpress/media/2012/10/R600_Instruction_Set_Architecture.pdf
// Microcode format differs, but most fields/enums are the same.

// This code comes from the freedreno project:
// https://github.com/freedreno/freedreno/blob/master/includes/instr-a2xx.h
/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

enum a2xx_sq_surfaceformat {
  FMT_1_REVERSE = 0,
  FMT_1 = 1,
  FMT_8 = 2,
  FMT_1_5_5_5 = 3,
  FMT_5_6_5 = 4,
  FMT_6_5_5 = 5,
  FMT_8_8_8_8 = 6,
  FMT_2_10_10_10 = 7,
  FMT_8_A = 8,
  FMT_8_B = 9,
  FMT_8_8 = 10,
  FMT_Cr_Y1_Cb_Y0 = 11,
  FMT_Y1_Cr_Y0_Cb = 12,
  FMT_5_5_5_1 = 13,
  FMT_8_8_8_8_A = 14,
  FMT_4_4_4_4 = 15,
  FMT_10_11_11 = 16,
  FMT_11_11_10 = 17,
  FMT_DXT1 = 18,
  FMT_DXT2_3 = 19,
  FMT_DXT4_5 = 20,
  FMT_24_8 = 22,
  FMT_24_8_FLOAT = 23,
  FMT_16 = 24,
  FMT_16_16 = 25,
  FMT_16_16_16_16 = 26,
  FMT_16_EXPAND = 27,
  FMT_16_16_EXPAND = 28,
  FMT_16_16_16_16_EXPAND = 29,
  FMT_16_FLOAT = 30,
  FMT_16_16_FLOAT = 31,
  FMT_16_16_16_16_FLOAT = 32,
  FMT_32 = 33,
  FMT_32_32 = 34,
  FMT_32_32_32_32 = 35,
  FMT_32_FLOAT = 36,
  FMT_32_32_FLOAT = 37,
  FMT_32_32_32_32_FLOAT = 38,
  FMT_32_AS_8 = 39,
  FMT_32_AS_8_8 = 40,
  FMT_16_MPEG = 41,
  FMT_16_16_MPEG = 42,
  FMT_8_INTERLACED = 43,
  FMT_32_AS_8_INTERLACED = 44,
  FMT_32_AS_8_8_INTERLACED = 45,
  FMT_16_INTERLACED = 46,
  FMT_16_MPEG_INTERLACED = 47,
  FMT_16_16_MPEG_INTERLACED = 48,
  FMT_DXN = 49,
  FMT_8_8_8_8_AS_16_16_16_16 = 50,
  FMT_DXT1_AS_16_16_16_16 = 51,
  FMT_DXT2_3_AS_16_16_16_16 = 52,
  FMT_DXT4_5_AS_16_16_16_16 = 53,
  FMT_2_10_10_10_AS_16_16_16_16 = 54,
  FMT_10_11_11_AS_16_16_16_16 = 55,
  FMT_11_11_10_AS_16_16_16_16 = 56,
  FMT_32_32_32_FLOAT = 57,
  FMT_DXT3A = 58,
  FMT_DXT5A = 59,
  FMT_CTX1 = 60,
  FMT_DXT3A_AS_1_1_1_1 = 61,
};

/*
 * ALU instructions:
 */

typedef enum {
  ADDs = 0,
  ADD_PREVs = 1,
  MULs = 2,
  MUL_PREVs = 3,
  MUL_PREV2s = 4,
  MAXs = 5,
  MINs = 6,
  SETEs = 7,
  SETGTs = 8,
  SETGTEs = 9,
  SETNEs = 10,
  FRACs = 11,
  TRUNCs = 12,
  FLOORs = 13,
  EXP_IEEE = 14,
  LOG_CLAMP = 15,
  LOG_IEEE = 16,
  RECIP_CLAMP = 17,
  RECIP_FF = 18,
  RECIP_IEEE = 19,
  RECIPSQ_CLAMP = 20,
  RECIPSQ_FF = 21,
  RECIPSQ_IEEE = 22,
  MOVAs = 23,
  MOVA_FLOORs = 24,
  SUBs = 25,
  SUB_PREVs = 26,
  PRED_SETEs = 27,
  PRED_SETNEs = 28,
  PRED_SETGTs = 29,
  PRED_SETGTEs = 30,
  PRED_SET_INVs = 31,
  PRED_SET_POPs = 32,
  PRED_SET_CLRs = 33,
  PRED_SET_RESTOREs = 34,
  KILLEs = 35,
  KILLGTs = 36,
  KILLGTEs = 37,
  KILLNEs = 38,
  KILLONEs = 39,
  SQRT_IEEE = 40,
  MUL_CONST_0 = 42,
  MUL_CONST_1 = 43,
  ADD_CONST_0 = 44,
  ADD_CONST_1 = 45,
  SUB_CONST_0 = 46,
  SUB_CONST_1 = 47,
  SIN = 48,
  COS = 49,
  RETAIN_PREV = 50,
} instr_scalar_opc_t;

typedef enum {
  ADDv = 0,
  MULv = 1,
  MAXv = 2,
  MINv = 3,
  SETEv = 4,
  SETGTv = 5,
  SETGTEv = 6,
  SETNEv = 7,
  FRACv = 8,
  TRUNCv = 9,
  FLOORv = 10,
  MULADDv = 11,
  CNDEv = 12,
  CNDGTEv = 13,
  CNDGTv = 14,
  DOT4v = 15,
  DOT3v = 16,
  DOT2ADDv = 17,
  CUBEv = 18,
  MAX4v = 19,
  PRED_SETE_PUSHv = 20,
  PRED_SETNE_PUSHv = 21,
  PRED_SETGT_PUSHv = 22,
  PRED_SETGTE_PUSHv = 23,
  KILLEv = 24,
  KILLGTv = 25,
  KILLGTEv = 26,
  KILLNEv = 27,
  DSTv = 28,
  MOVAv = 29,
} instr_vector_opc_t;

XEPACKEDSTRUCT(instr_alu_t, {
  /* dword0: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t vector_dest : 6;
    uint32_t vector_dest_rel : 1;
    uint32_t abs_constants : 1;
    uint32_t scalar_dest : 6;
    uint32_t scalar_dest_rel : 1;
    uint32_t export_data : 1;
    uint32_t vector_write_mask : 4;
    uint32_t scalar_write_mask : 4;
    uint32_t vector_clamp : 1;
    uint32_t scalar_clamp : 1;
    uint32_t scalar_opc : 6;  // instr_scalar_opc_t
  });
  /* dword1: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t src3_swiz : 8;
    uint32_t src2_swiz : 8;
    uint32_t src1_swiz : 8;
    uint32_t src3_reg_negate : 1;
    uint32_t src2_reg_negate : 1;
    uint32_t src1_reg_negate : 1;
    uint32_t pred_condition : 1;
    uint32_t pred_select : 1;
    uint32_t relative_addr : 1;
    uint32_t const_1_rel_abs : 1;
    uint32_t const_0_rel_abs : 1;
  });
  /* dword2: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t src3_reg : 8;
    uint32_t src2_reg : 8;
    uint32_t src1_reg : 8;
    uint32_t vector_opc : 5;  // instr_vector_opc_t
    uint32_t src3_sel : 1;
    uint32_t src2_sel : 1;
    uint32_t src1_sel : 1;
  });
});

/*
 * CF instructions:
 */

typedef enum {
  NOP = 0,
  EXEC = 1,
  EXEC_END = 2,
  COND_EXEC = 3,
  COND_EXEC_END = 4,
  COND_PRED_EXEC = 5,
  COND_PRED_EXEC_END = 6,
  LOOP_START = 7,
  LOOP_END = 8,
  COND_CALL = 9,
  RETURN = 10,
  COND_JMP = 11,
  ALLOC = 12,
  COND_EXEC_PRED_CLEAN = 13,
  COND_EXEC_PRED_CLEAN_END = 14,
  MARK_VS_FETCH_DONE = 15,
} instr_cf_opc_t;

typedef enum {
  RELATIVE_ADDR = 0,
  ABSOLUTE_ADDR = 1,
} instr_addr_mode_t;

typedef enum {
  SQ_NO_ALLOC = 0,
  SQ_POSITION = 1,
  SQ_PARAMETER_PIXEL = 2,
  SQ_MEMORY = 3,
} instr_alloc_type_t;

XEPACKEDSTRUCT(instr_cf_exec_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t address : 12;
    uint32_t count : 3;
    uint32_t yeild : 1;
    uint32_t serialize : 12;
    uint32_t vc_hi : 4;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t vc_lo : 2; /* vertex cache? */
    uint32_t bool_addr : 8;
    uint32_t pred_condition : 1;
    uint32_t address_mode : 1;  // instr_addr_mode_t
    uint32_t opc : 4;           // instr_cf_opc_t
  });
  bool is_cond_exec() const {
    return (this->opc == COND_EXEC) || (this->opc == COND_EXEC_END) ||
           (this->opc == COND_PRED_EXEC) || (this->opc == COND_PRED_EXEC_END) ||
           (this->opc == COND_EXEC_PRED_CLEAN) ||
           (this->opc == COND_EXEC_PRED_CLEAN_END);
  }
});

XEPACKEDSTRUCT(instr_cf_loop_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t address : 13;
    uint32_t repeat : 1;
    uint32_t reserved0 : 2;
    uint32_t loop_id : 5;
    uint32_t pred_break : 1;
    uint32_t reserved1_hi : 10;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t reserved1_lo : 10;
    uint32_t condition : 1;
    uint32_t address_mode : 1;  // instr_addr_mode_t
    uint32_t opc : 4;           // instr_cf_opc_t
  });
});

XEPACKEDSTRUCT(instr_cf_jmp_call_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t address : 13;
    uint32_t force_call : 1;
    uint32_t predicated_jmp : 1;
    uint32_t reserved1_hi : 17;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t reserved1_lo : 1;
    uint32_t direction : 1;
    uint32_t bool_addr : 8;
    uint32_t condition : 1;
    uint32_t address_mode : 1;  // instr_addr_mode_t
    uint32_t opc : 4;           // instr_cf_opc_t
  });
});

XEPACKEDSTRUCT(instr_cf_alloc_t, {
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t size : 3;
    uint32_t reserved0_hi : 29;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t reserved0_lo : 8;
    uint32_t no_serial : 1;
    uint32_t buffer_select : 2;  // instr_alloc_type_t
    uint32_t alloc_mode : 1;
    uint32_t opc : 4;  // instr_cf_opc_t
  });
});

XEPACKEDUNION(instr_cf_t, {
  instr_cf_exec_t exec;
  instr_cf_loop_t loop;
  instr_cf_jmp_call_t jmp_call;
  instr_cf_alloc_t alloc;
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t:
      32;
    uint32_t:
      12;
      uint32_t opc : 4;  // instr_cf_opc_t
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
  });

  bool is_exec() const {
    return (this->opc == EXEC) || (this->opc == EXEC_END) ||
           (this->opc == COND_EXEC) || (this->opc == COND_EXEC_END) ||
           (this->opc == COND_PRED_EXEC) || (this->opc == COND_PRED_EXEC_END) ||
           (this->opc == COND_EXEC_PRED_CLEAN) ||
           (this->opc == COND_EXEC_PRED_CLEAN_END);
  }
  bool is_cond_exec() const {
    return (this->opc == COND_EXEC) || (this->opc == COND_EXEC_END) ||
           (this->opc == COND_PRED_EXEC) || (this->opc == COND_PRED_EXEC_END) ||
           (this->opc == COND_EXEC_PRED_CLEAN) ||
           (this->opc == COND_EXEC_PRED_CLEAN_END);
  }
});

/*
 * FETCH instructions:
 */

typedef enum {
  VTX_FETCH = 0,
  TEX_FETCH = 1,
  TEX_GET_BORDER_COLOR_FRAC = 16,
  TEX_GET_COMP_TEX_LOD = 17,
  TEX_GET_GRADIENTS = 18,
  TEX_GET_WEIGHTS = 19,
  TEX_SET_TEX_LOD = 24,
  TEX_SET_GRADIENTS_H = 25,
  TEX_SET_GRADIENTS_V = 26,
  TEX_RESERVED_4 = 27,
} instr_fetch_opc_t;

typedef enum {
  TEX_FILTER_POINT = 0,
  TEX_FILTER_LINEAR = 1,
  TEX_FILTER_BASEMAP = 2, /* only applicable for mip-filter */
  TEX_FILTER_USE_FETCH_CONST = 3,
} instr_tex_filter_t;

typedef enum {
  ANISO_FILTER_DISABLED = 0,
  ANISO_FILTER_MAX_1_1 = 1,
  ANISO_FILTER_MAX_2_1 = 2,
  ANISO_FILTER_MAX_4_1 = 3,
  ANISO_FILTER_MAX_8_1 = 4,
  ANISO_FILTER_MAX_16_1 = 5,
  ANISO_FILTER_USE_FETCH_CONST = 7,
} instr_aniso_filter_t;

typedef enum {
  ARBITRARY_FILTER_2X4_SYM = 0,
  ARBITRARY_FILTER_2X4_ASYM = 1,
  ARBITRARY_FILTER_4X2_SYM = 2,
  ARBITRARY_FILTER_4X2_ASYM = 3,
  ARBITRARY_FILTER_4X4_SYM = 4,
  ARBITRARY_FILTER_4X4_ASYM = 5,
  ARBITRARY_FILTER_USE_FETCH_CONST = 7,
} instr_arbitrary_filter_t;

typedef enum {
  SAMPLE_CENTROID = 0,
  SAMPLE_CENTER = 1,
} instr_sample_loc_t;

typedef enum {
  DIMENSION_1D = 0,
  DIMENSION_2D = 1,
  DIMENSION_3D = 2,
  DIMENSION_CUBE = 3,
} instr_dimension_t;

typedef enum a2xx_sq_surfaceformat instr_surf_fmt_t;

XEPACKEDSTRUCT(instr_fetch_tex_t, {
  /* dword0: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t opc : 5;  // instr_fetch_opc_t
    uint32_t src_reg : 6;
    uint32_t src_reg_am : 1;
    uint32_t dst_reg : 6;
    uint32_t dst_reg_am : 1;
    uint32_t fetch_valid_only : 1;
    uint32_t const_idx : 5;
    uint32_t tx_coord_denorm : 1;
    uint32_t src_swiz : 6;  // xyz
  });
  /* dword1: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dst_swiz : 12;         // xyzw
    uint32_t mag_filter : 2;        // instr_tex_filter_t
    uint32_t min_filter : 2;        // instr_tex_filter_t
    uint32_t mip_filter : 2;        // instr_tex_filter_t
    uint32_t aniso_filter : 3;      // instr_aniso_filter_t
    uint32_t arbitrary_filter : 3;  // instr_arbitrary_filter_t
    uint32_t vol_mag_filter : 2;    // instr_tex_filter_t
    uint32_t vol_min_filter : 2;    // instr_tex_filter_t
    uint32_t use_comp_lod : 1;
    uint32_t use_reg_lod : 1;
    uint32_t unk : 1;
    uint32_t pred_select : 1;
  });
  /* dword2: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t use_reg_gradients : 1;
    uint32_t sample_location : 1;  // instr_sample_loc_t
    uint32_t lod_bias : 7;
    uint32_t unused : 5;
    uint32_t dimension : 2;  // instr_dimension_t
    uint32_t offset_x : 5;
    uint32_t offset_y : 5;
    uint32_t offset_z : 5;
    uint32_t pred_condition : 1;
  });
});

XEPACKEDSTRUCT(instr_fetch_vtx_t, {
  /* dword0: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t opc : 5;  // instr_fetch_opc_t
    uint32_t src_reg : 6;
    uint32_t src_reg_am : 1;
    uint32_t dst_reg : 6;
    uint32_t dst_reg_am : 1;
    uint32_t must_be_one : 1;
    uint32_t const_index : 5;
    uint32_t const_index_sel : 2;
    uint32_t reserved0 : 3;
    uint32_t src_swiz : 2;
  });
  /* dword1: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dst_swiz : 12;
    uint32_t format_comp_all : 1; /* '1' for signed, '0' for unsigned? */
    uint32_t num_format_all : 1;  /* '0' for normalized, '1' for unnormalized */
    uint32_t signed_rf_mode_all : 1;
    uint32_t reserved1 : 1;
    uint32_t format : 6;  // instr_surf_fmt_t
    uint32_t reserved2 : 1;
    uint32_t exp_adjust_all : 7;
    uint32_t reserved3 : 1;
    uint32_t pred_select : 1;
  });
  /* dword2: */
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t stride : 8;
    uint32_t offset : 23;
    uint32_t pred_condition : 1;
  });
});

XEPACKEDUNION(instr_fetch_t, {
  instr_fetch_tex_t tex;
  instr_fetch_vtx_t vtx;
  XEPACKEDSTRUCTANONYMOUS({
    /* dword0: */
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t opc : 5;  // instr_fetch_opc_t
    uint32_t:
      27;
    });
    /* dword1: */
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t:
        32;
    });
    /* dword2: */
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t:
        32;
    });
  });
});

}  // namespace ucode

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_UCODE_H_
