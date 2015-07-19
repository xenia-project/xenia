/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

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

#include "xenia/gpu/ucode_disassembler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "xenia/base/assert.h"
#include "xenia/base/string_buffer.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::ucode;
using namespace xe::gpu::xenos;

static const char* levels[] = {
    "",
    "\t",
    "\t\t",
    "\t\t\t",
    "\t\t\t\t",
    "\t\t\t\t\t",
    "\t\t\t\t\t\t",
    "\t\t\t\t\t\t\t",
    "\t\t\t\t\t\t\t\t",
    "\t\t\t\t\t\t\t\t\t",
    "x",
    "x",
    "x",
    "x",
    "x",
    "x",
};

/*
 * ALU instructions:
 */

static const char chan_names[] = {
    'x', 'y', 'z', 'w',
    /* these only apply to FETCH dst's: */
    '0', '1', '?', '_',
};

void print_srcreg(StringBuffer* output, uint32_t num, uint32_t type,
                  uint32_t swiz, uint32_t negate, uint32_t abs_constants,
                  ShaderType shader_type) {
  if (negate) {
    output->Append('-');
  }
  if (type) {
    if (num & 0x80) {
      output->Append("abs(");
    }
    output->AppendFormat("R%u", num & 0x7F);
    if (num & 0x80) {
      output->Append(')');
    }
  } else {
    if (abs_constants) {
      output->Append('|');
    }
    num += shader_type == ShaderType::kPixel ? 256 : 0;
    output->AppendFormat("C%u", num);
    if (abs_constants) {
      output->Append('|');
    }
  }
  if (swiz) {
    output->Append('.');
    for (int i = 0; i < 4; i++) {
      output->Append(chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
}

void print_dstreg(StringBuffer* output, uint32_t num, uint32_t mask,
                  uint32_t dst_exp) {
  output->AppendFormat("%s%u", dst_exp ? "export" : "R", num);
  if (mask != 0xf) {
    output->Append('.');
    for (int i = 0; i < 4; i++) {
      output->Append((mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void print_export_comment(StringBuffer* output, uint32_t num, ShaderType type) {
  const char* name = NULL;
  switch (type) {
    case ShaderType::kVertex:
      switch (num) {
        case 62:
          name = "gl_Position";
          break;
        case 63:
          name = "gl_PointSize";
          break;
      }
      break;
    case ShaderType::kPixel:
      switch (num) {
        case 0:
          name = "gl_FragColor";
          break;
      }
      break;
  }
  /* if we had a symbol table here, we could look
   * up the name of the varying..
   */
  if (name) {
    output->AppendFormat("\t; %s", name);
  }
}

struct {
  uint32_t num_srcs;
  const char* name;
} vector_instructions[0x20] =
    {
#define INSTR(opc, num_srcs) \
  { num_srcs, #opc }
        INSTR(ADDv, 2),               // 0
        INSTR(MULv, 2),               // 1
        INSTR(MAXv, 2),               // 2
        INSTR(MINv, 2),               // 3
        INSTR(SETEv, 2),              // 4
        INSTR(SETGTv, 2),             // 5
        INSTR(SETGTEv, 2),            // 6
        INSTR(SETNEv, 2),             // 7
        INSTR(FRACv, 1),              // 8
        INSTR(TRUNCv, 1),             // 9
        INSTR(FLOORv, 1),             // 10
        INSTR(MULADDv, 3),            // 111
        INSTR(CNDEv, 3),              // 12
        INSTR(CNDGTEv, 3),            // 13
        INSTR(CNDGTv, 3),             // 14
        INSTR(DOT4v, 2),              // 15
        INSTR(DOT3v, 2),              // 16
        INSTR(DOT2ADDv, 3),           // 17 -- ???
        INSTR(CUBEv, 2),              // 18
        INSTR(MAX4v, 1),              // 19
        INSTR(PRED_SETE_PUSHv, 2),    // 20
        INSTR(PRED_SETNE_PUSHv, 2),   // 21
        INSTR(PRED_SETGT_PUSHv, 2),   // 22
        INSTR(PRED_SETGTE_PUSHv, 2),  // 23
        INSTR(KILLEv, 2),             // 24
        INSTR(KILLGTv, 2),            // 25
        INSTR(KILLGTEv, 2),           // 26
        INSTR(KILLNEv, 2),            // 27
        INSTR(DSTv, 2),               // 28
        INSTR(MOVAv, 1),              // 29
},
  scalar_instructions[0x40] = {
      INSTR(ADDs, 1),               // 0
      INSTR(ADD_PREVs, 1),          // 1
      INSTR(MULs, 1),               // 2
      INSTR(MUL_PREVs, 1),          // 3
      INSTR(MUL_PREV2s, 1),         // 4
      INSTR(MAXs, 1),               // 5
      INSTR(MINs, 1),               // 6
      INSTR(SETEs, 1),              // 7
      INSTR(SETGTs, 1),             // 8
      INSTR(SETGTEs, 1),            // 9
      INSTR(SETNEs, 1),             // 10
      INSTR(FRACs, 1),              // 11
      INSTR(TRUNCs, 1),             // 12
      INSTR(FLOORs, 1),             // 13
      INSTR(EXP_IEEE, 1),           // 14
      INSTR(LOG_CLAMP, 1),          // 15
      INSTR(LOG_IEEE, 1),           // 16
      INSTR(RECIP_CLAMP, 1),        // 17
      INSTR(RECIP_FF, 1),           // 18
      INSTR(RECIP_IEEE, 1),         // 19
      INSTR(RECIPSQ_CLAMP, 1),      // 20
      INSTR(RECIPSQ_FF, 1),         // 21
      INSTR(RECIPSQ_IEEE, 1),       // 22
      INSTR(MOVAs, 1),              // 23
      INSTR(MOVA_FLOORs, 1),        // 24
      INSTR(SUBs, 1),               // 25
      INSTR(SUB_PREVs, 1),          // 26
      INSTR(PRED_SETEs, 1),         // 27
      INSTR(PRED_SETNEs, 1),        // 28
      INSTR(PRED_SETGTs, 1),        // 29
      INSTR(PRED_SETGTEs, 1),       // 30
      INSTR(PRED_SET_INVs, 1),      // 31
      INSTR(PRED_SET_POPs, 1),      // 32
      INSTR(PRED_SET_CLRs, 1),      // 33
      INSTR(PRED_SET_RESTOREs, 1),  // 34
      INSTR(KILLEs, 1),             // 35
      INSTR(KILLGTs, 1),            // 36
      INSTR(KILLGTEs, 1),           // 37
      INSTR(KILLNEs, 1),            // 38
      INSTR(KILLONEs, 1),           // 39
      INSTR(SQRT_IEEE, 1),          // 40
      {0, 0},
      INSTR(MUL_CONST_0, 2),  // 42
      INSTR(MUL_CONST_1, 2),  // 43
      INSTR(ADD_CONST_0, 2),  // 44
      INSTR(ADD_CONST_1, 2),  // 45
      INSTR(SUB_CONST_0, 2),  // 46
      INSTR(SUB_CONST_1, 2),  // 47
      INSTR(SIN, 1),          // 48
      INSTR(COS, 1),          // 49
      INSTR(RETAIN_PREV, 1),  // 50
#undef INSTR
};

int disasm_alu(StringBuffer* output, const uint32_t* dwords, uint32_t alu_off,
               int level, int sync, ShaderType type) {
  const instr_alu_t* alu = (const instr_alu_t*)dwords;

  output->Append(levels[level]);
  output->AppendFormat("%02x: %08x %08x %08x\t", alu_off, dwords[0], dwords[1],
                       dwords[2]);

  output->AppendFormat("   %sALU:\t", sync ? "(S)" : "   ");

  if (!alu->scalar_write_mask && !alu->vector_write_mask) {
    output->Append("   <nop>\n");
  }

  if (alu->vector_write_mask) {
    output->Append(vector_instructions[alu->vector_opc].name);

    if (alu->pred_select & 0x2) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      output->Append((alu->pred_select & 0x1) ? "EQ" : "NE");
    }

    output->Append("\t");

    print_dstreg(output, alu->vector_dest, alu->vector_write_mask,
                 alu->export_data);
    output->Append(" = ");
    if (vector_instructions[alu->vector_opc].num_srcs == 3) {
      print_srcreg(output, alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                   alu->src3_reg_negate, alu->abs_constants, type);
      output->Append(", ");
    }
    print_srcreg(output, alu->src1_reg, alu->src1_sel, alu->src1_swiz,
                 alu->src1_reg_negate, alu->abs_constants, type);
    if (vector_instructions[alu->vector_opc].num_srcs > 1) {
      output->Append(", ");
      print_srcreg(output, alu->src2_reg, alu->src2_sel, alu->src2_swiz,
                   alu->src2_reg_negate, alu->abs_constants, type);
    }

    if (alu->vector_clamp) {
      output->Append(" CLAMP");
    }
    if (alu->pred_select) {
      output->AppendFormat(" COND(%d)", alu->pred_condition);
    }
    if (alu->export_data) {
      print_export_comment(output, alu->vector_dest, type);
    }

    output->Append('\n');
  }

  if (alu->scalar_write_mask || !alu->vector_write_mask) {
    // 2nd optional scalar op:

    if (alu->vector_write_mask) {
      output->Append(levels[level]);
      output->AppendFormat("                          \t\t\t\t\t\t    \t");
    }

    if (scalar_instructions[alu->scalar_opc].name) {
      output->AppendFormat("%s\t", scalar_instructions[alu->scalar_opc].name);
    } else {
      output->AppendFormat("OP(%u)\t", alu->scalar_opc);
    }

    print_dstreg(output, alu->scalar_dest, alu->scalar_write_mask,
                 alu->export_data);
    output->Append(" = ");
    if (scalar_instructions[alu->scalar_opc].num_srcs == 2) {
      // MUL/ADD/etc
      // Clever, CONST_0 and CONST_1 are just an extra storage bit.
      // ADD_CONST_0 dest, [const], [reg]
      uint32_t src3_swiz = alu->src3_swiz & ~0x3C;
      uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
      uint32_t swiz_b = (src3_swiz & 0x3);
      print_srcreg(output, alu->src3_reg, 0, 0, alu->src3_reg_negate,
                   alu->abs_constants, type);
      output->AppendFormat(".%c", chan_names[swiz_a]);
      output->Append(", ");
      uint32_t reg2 = (alu->scalar_opc & 1) | (alu->src3_swiz & 0x3C) |
                      (alu->src3_sel << 1);
      print_srcreg(output, reg2, 1, 0, alu->src3_reg_negate, alu->abs_constants,
                   type);
      output->AppendFormat(".%c", chan_names[swiz_b]);
    } else {
      print_srcreg(output, alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                   alu->src3_reg_negate, alu->abs_constants, type);
    }
    if (alu->scalar_clamp) {
      output->Append(" CLAMP");
    }
    if (alu->export_data) {
      print_export_comment(output, alu->scalar_dest, type);
    }
    output->Append('\n');
  }

  return 0;
}

struct {
  const char* name;
} fetch_types[0xff] = {
#define TYPE(id) \
  { #id }
    TYPE(FMT_1_REVERSE),  // 0
    {0},
    TYPE(FMT_8),  // 2
    {0},
    {0},
    {0},
    TYPE(FMT_8_8_8_8),     // 6
    TYPE(FMT_2_10_10_10),  // 7
    {0},
    {0},
    TYPE(FMT_8_8),  // 10
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_16),           // 24
    TYPE(FMT_16_16),        // 25
    TYPE(FMT_16_16_16_16),  // 26
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_32),                 // 33
    TYPE(FMT_32_32),              // 34
    TYPE(FMT_32_32_32_32),        // 35
    TYPE(FMT_32_FLOAT),           // 36
    TYPE(FMT_32_32_FLOAT),        // 37
    TYPE(FMT_32_32_32_32_FLOAT),  // 38
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_32_32_32_FLOAT),  // 57
#undef TYPE
};

void print_fetch_dst(StringBuffer* output, uint32_t dst_reg,
                     uint32_t dst_swiz) {
  output->AppendFormat("\tR%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    output->Append(chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void print_fetch_vtx(StringBuffer* output, const instr_fetch_t* fetch) {
  const instr_fetch_vtx_t* vtx = &fetch->vtx;

  if (vtx->pred_select) {
    // seems to work similar to conditional execution in ARM instruction
    // set, so let's use a similar syntax for now:
    output->Append(vtx->pred_condition ? "EQ" : "NE");
  }

  print_fetch_dst(output, vtx->dst_reg, vtx->dst_swiz);
  output->AppendFormat(" = R%u.", vtx->src_reg);
  output->Append(chan_names[vtx->src_swiz & 0x3]);
  if (fetch_types[vtx->format].name) {
    output->AppendFormat(" %s", fetch_types[vtx->format].name);
  } else {
    output->AppendFormat(" TYPE(0x%x)", vtx->format);
  }
  output->AppendFormat(" %s", vtx->format_comp_all ? "SIGNED" : "UNSIGNED");
  if (!vtx->num_format_all) {
    output->Append(" NORMALIZED");
  }
  output->AppendFormat(" STRIDE(%u)", vtx->stride);
  if (vtx->offset) {
    output->AppendFormat(" OFFSET(%u)", vtx->offset);
  }
  output->AppendFormat(" CONST(%u, %u)", vtx->const_index,
                       vtx->const_index_sel);
  if (vtx->pred_select) {
    output->AppendFormat(" COND(%d)", vtx->pred_condition);
  }
  if (1) {
    // XXX
    output->AppendFormat(" src_reg_am=%u", vtx->src_reg_am);
    output->AppendFormat(" dst_reg_am=%u", vtx->dst_reg_am);
    output->AppendFormat(" num_format_all=%u", vtx->num_format_all);
    output->AppendFormat(" signed_rf_mode_all=%u", vtx->signed_rf_mode_all);
    output->AppendFormat(" exp_adjust_all=%u", vtx->exp_adjust_all);
  }
}

void print_fetch_tex(StringBuffer* output, const instr_fetch_t* fetch) {
  static const char* filter[] = {
      "POINT",    // TEX_FILTER_POINT
      "LINEAR",   // TEX_FILTER_LINEAR
      "BASEMAP",  // TEX_FILTER_BASEMAP
  };
  static const char* aniso_filter[] = {
      "DISABLED",  // ANISO_FILTER_DISABLED
      "MAX_1_1",   // ANISO_FILTER_MAX_1_1
      "MAX_2_1",   // ANISO_FILTER_MAX_2_1
      "MAX_4_1",   // ANISO_FILTER_MAX_4_1
      "MAX_8_1",   // ANISO_FILTER_MAX_8_1
      "MAX_16_1",  // ANISO_FILTER_MAX_16_1
  };
  static const char* arbitrary_filter[] = {
      "2x4_SYM",   // ARBITRARY_FILTER_2X4_SYM
      "2x4_ASYM",  // ARBITRARY_FILTER_2X4_ASYM
      "4x2_SYM",   // ARBITRARY_FILTER_4X2_SYM
      "4x2_ASYM",  // ARBITRARY_FILTER_4X2_ASYM
      "4x4_SYM",   // ARBITRARY_FILTER_4X4_SYM
      "4x4_ASYM",  // ARBITRARY_FILTER_4X4_ASYM
  };
  static const char* sample_loc[] = {
      "CENTROID",  // SAMPLE_CENTROID
      "CENTER",    // SAMPLE_CENTER
  };
  const instr_fetch_tex_t* tex = &fetch->tex;
  uint32_t src_swiz = tex->src_swiz;

  if (tex->pred_select) {
    // seems to work similar to conditional execution in ARM instruction
    // set, so let's use a similar syntax for now:
    output->Append(tex->pred_condition ? "EQ" : "NE");
  }

  print_fetch_dst(output, tex->dst_reg, tex->dst_swiz);
  output->AppendFormat(" = R%u.", tex->src_reg);
  for (int i = 0; i < 3; i++) {
    output->Append(chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  output->AppendFormat(" CONST(%u)", tex->const_idx);
  if (tex->fetch_valid_only) {
    output->Append(" VALID_ONLY");
  }
  if (tex->tx_coord_denorm) {
    output->Append(" DENORM");
  }
  if (tex->mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" MAG(%s)", filter[tex->mag_filter]);
  }
  if (tex->min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" MIN(%s)", filter[tex->min_filter]);
  }
  if (tex->mip_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" MIP(%s)", filter[tex->mip_filter]);
  }
  if (tex->aniso_filter != ANISO_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" ANISO(%s)", aniso_filter[tex->aniso_filter]);
  }
  if (tex->arbitrary_filter != ARBITRARY_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" ARBITRARY(%s)",
                         arbitrary_filter[tex->arbitrary_filter]);
  }
  if (tex->vol_mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" VOL_MAG(%s)", filter[tex->vol_mag_filter]);
  }
  if (tex->vol_min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->AppendFormat(" VOL_MIN(%s)", filter[tex->vol_min_filter]);
  }
  if (!tex->use_comp_lod) {
    output->AppendFormat(" LOD(%u)", tex->use_comp_lod);
    output->AppendFormat(" LOD_BIAS(%u)", tex->lod_bias);
  }
  if (tex->use_reg_lod) {
    output->AppendFormat(" REG_LOD(%u)", tex->use_reg_lod);
  }
  if (tex->use_reg_gradients) {
    output->Append(" USE_REG_GRADIENTS");
  }
  output->AppendFormat(" LOCATION(%s)", sample_loc[tex->sample_location]);
  if (tex->offset_x || tex->offset_y || tex->offset_z) {
    output->AppendFormat(" OFFSET(%u,%u,%u)", tex->offset_x, tex->offset_y,
                         tex->offset_z);
  }
  if (tex->pred_select) {
    output->AppendFormat(" COND(%d)", tex->pred_condition);
  }
}

struct {
  const char* name;
  void (*fxn)(StringBuffer* output, const instr_fetch_t* cf);
} fetch_instructions[] = {
#define INSTR(opc, name, fxn) \
  { name, fxn }
    INSTR(VTX_FETCH, "VERTEX", print_fetch_vtx),  // 0
    INSTR(TEX_FETCH, "SAMPLE", print_fetch_tex),  // 1
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    INSTR(TEX_GET_BORDER_COLOR_FRAC, "?", print_fetch_tex),  // 16
    INSTR(TEX_GET_COMP_TEX_LOD, "?", print_fetch_tex),       // 17
    INSTR(TEX_GET_GRADIENTS, "?", print_fetch_tex),          // 18
    INSTR(TEX_GET_WEIGHTS, "?", print_fetch_tex),            // 19
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    INSTR(TEX_SET_TEX_LOD, "SET_TEX_LOD", print_fetch_tex),  // 24
    INSTR(TEX_SET_GRADIENTS_H, "?", print_fetch_tex),        // 25
    INSTR(TEX_SET_GRADIENTS_V, "?", print_fetch_tex),        // 26
    INSTR(TEX_RESERVED_4, "?", print_fetch_tex),             // 27
#undef INSTR
};

int disasm_fetch(StringBuffer* output, const uint32_t* dwords, uint32_t alu_off,
                 int level, int sync) {
  const instr_fetch_t* fetch = (const instr_fetch_t*)dwords;

  output->Append(levels[level]);
  output->AppendFormat("%02x: %08x %08x %08x\t", alu_off, dwords[0], dwords[1],
                       dwords[2]);

  output->AppendFormat("   %sFETCH:\t", sync ? "(S)" : "   ");
  if (fetch_instructions[fetch->opc].fxn) {
    output->Append(fetch_instructions[fetch->opc].name);
    fetch_instructions[fetch->opc].fxn(output, fetch);
  } else {
    output->Append("???");
  }
  output->Append('\n');

  return 0;
}

void print_cf_nop(StringBuffer* output, const instr_cf_t* cf) {}

void print_cf_exec(StringBuffer* output, const instr_cf_t* cf) {
  output->AppendFormat(" ADDR(0x%x) CNT(0x%x)", cf->exec.address,
                       cf->exec.count);
  if (cf->exec.yeild) {
    output->Append(" YIELD");
  }
  uint8_t vc = uint8_t(cf->exec.vc_hi | (cf->exec.vc_lo << 2));
  if (vc) {
    output->AppendFormat(" VC(0x%x)", vc);
  }
  if (cf->exec.bool_addr) {
    output->AppendFormat(" BOOL_ADDR(0x%x)", cf->exec.bool_addr);
  }
  if (cf->exec.address_mode == ABSOLUTE_ADDR) {
    output->Append(" ABSOLUTE_ADDR");
  }
  if (cf->is_cond_exec()) {
    output->AppendFormat(" COND(%d)", cf->exec.pred_condition);
  }
}

void print_cf_loop(StringBuffer* output, const instr_cf_t* cf) {
  output->AppendFormat(" ADDR(0x%x) LOOP_ID(%d)", cf->loop.address,
                       cf->loop.loop_id);
  if (cf->loop.address_mode == ABSOLUTE_ADDR) {
    output->Append(" ABSOLUTE_ADDR");
  }
}

void print_cf_jmp_call(StringBuffer* output, const instr_cf_t* cf) {
  output->AppendFormat(" ADDR(0x%x) DIR(%d)", cf->jmp_call.address,
                       cf->jmp_call.direction);
  if (cf->jmp_call.force_call) {
    output->Append(" FORCE_CALL");
  }
  if (cf->jmp_call.predicated_jmp) {
    output->AppendFormat(" COND(%d)", cf->jmp_call.condition);
  }
  if (cf->jmp_call.bool_addr) {
    output->AppendFormat(" BOOL_ADDR(0x%x)", cf->jmp_call.bool_addr);
  }
  if (cf->jmp_call.address_mode == ABSOLUTE_ADDR) {
    output->Append(" ABSOLUTE_ADDR");
  }
}

void print_cf_alloc(StringBuffer* output, const instr_cf_t* cf) {
  static const char* bufname[] = {
      "NO ALLOC",     // SQ_NO_ALLOC
      "POSITION",     // SQ_POSITION
      "PARAM/PIXEL",  // SQ_PARAMETER_PIXEL
      "MEMORY",       // SQ_MEMORY
  };
  output->AppendFormat(" %s SIZE(0x%x)", bufname[cf->alloc.buffer_select],
                       cf->alloc.size);
  if (cf->alloc.no_serial) {
    output->Append(" NO_SERIAL");
  }
  if (cf->alloc.alloc_mode) {
    // ???
    output->Append(" ALLOC_MODE");
  }
}

struct {
  const char* name;
  void (*fxn)(StringBuffer* output, const instr_cf_t* cf);
} cf_instructions[] = {
#define INSTR(opc, fxn) \
  { #opc, fxn }
    INSTR(NOP, print_cf_nop),                        //
    INSTR(EXEC, print_cf_exec),                      //
    INSTR(EXEC_END, print_cf_exec),                  //
    INSTR(COND_EXEC, print_cf_exec),                 //
    INSTR(COND_EXEC_END, print_cf_exec),             //
    INSTR(COND_PRED_EXEC, print_cf_exec),            //
    INSTR(COND_PRED_EXEC_END, print_cf_exec),        //
    INSTR(LOOP_START, print_cf_loop),                //
    INSTR(LOOP_END, print_cf_loop),                  //
    INSTR(COND_CALL, print_cf_jmp_call),             //
    INSTR(RETURN, print_cf_jmp_call),                //
    INSTR(COND_JMP, print_cf_jmp_call),              //
    INSTR(ALLOC, print_cf_alloc),                    //
    INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),      //
    INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),  //
    INSTR(MARK_VS_FETCH_DONE, print_cf_nop),         // ??
#undef INSTR
};

static void print_cf(StringBuffer* output, const instr_cf_t* cf, int level) {
  output->Append(levels[level]);

  const uint16_t* words = (uint16_t*)cf;
  output->AppendFormat("    %04x %04x %04x            \t", words[0], words[1],
                       words[2]);

  output->AppendFormat(cf_instructions[cf->opc].name);
  cf_instructions[cf->opc].fxn(output, cf);
  output->Append('\n');
}

/*
 * The adreno shader microcode consists of two parts:
 *   1) A CF (control-flow) program, at the header of the compiled shader,
 *      which refers to ALU/FETCH instructions that follow it by address.
 *   2) ALU and FETCH instructions
 */
void disasm_exec(StringBuffer* output, const uint32_t* dwords,
                 size_t dword_count, int level, ShaderType type,
                 const instr_cf_t* cf) {
  uint32_t sequence = cf->exec.serialize;
  for (uint32_t i = 0; i < cf->exec.count; i++) {
    uint32_t alu_off = (cf->exec.address + i);
    if (sequence & 0x1) {
      disasm_fetch(output, dwords + alu_off * 3, alu_off, level,
                   sequence & 0x2);
    } else {
      disasm_alu(output, dwords + alu_off * 3, alu_off, level, sequence & 0x2,
                 type);
    }
    sequence >>= 2;
  }
}

std::string DisassembleShader(ShaderType type, const uint32_t* dwords,
                              size_t dword_count) {
  StringBuffer string_buffer(256 * 1024);

  instr_cf_t cfa;
  instr_cf_t cfb;
  for (int idx = 0; idx < dword_count; idx += 3) {
    uint32_t dword_0 = dwords[idx + 0];
    uint32_t dword_1 = dwords[idx + 1];
    uint32_t dword_2 = dwords[idx + 2];
    cfa.dword_0 = dword_0;
    cfa.dword_1 = dword_1 & 0xFFFF;
    cfb.dword_0 = (dword_1 >> 16) | (dword_2 << 16);
    cfb.dword_1 = dword_2 >> 16;
    print_cf(&string_buffer, &cfa, 0);
    if (cfa.is_exec()) {
      disasm_exec(&string_buffer, dwords, dword_count, 0, type, &cfa);
    }
    print_cf(&string_buffer, &cfb, 0);
    if (cfb.is_exec()) {
      disasm_exec(&string_buffer, dwords, dword_count, 0, type, &cfb);
    }
    if (cfa.opc == EXEC_END || cfb.opc == EXEC_END) {
      break;
    }
  }

  return string_buffer.to_string();
}

}  // namespace gpu
}  // namespace xe
