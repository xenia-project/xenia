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

#include <xenia/gpu/xenos/ucode_disassembler.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


namespace {

const int OUTPUT_CAPACITY = 32 * 1024;
struct Output {
  char buffer[OUTPUT_CAPACITY];
  size_t capacity;
  size_t offset;
  Output() :
      capacity(OUTPUT_CAPACITY),
      offset(0) {
    buffer[0] = 0;
  }
  void append(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = xevsnprintfa(
        buffer + offset, capacity - offset, format, args);
    va_end(args);
    offset += len;
    buffer[offset] = 0;
  }
};

static const char *levels[] = {
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

void print_srcreg(
    Output* output,
    uint32_t num, uint32_t type,
    uint32_t swiz, uint32_t negate, uint32_t abs) {
  if (negate) {
    output->append("-");
  }
  if (abs) {
    output->append("|");
  }
  output->append("%c%u", type ? 'R' : 'C', num);
  if (swiz) {
    output->append(".");
    for (int i = 0; i < 4; i++) {
      output->append("%c", chan_names[(swiz + i) & 0x3]);
      swiz >>= 2;
    }
  }
  if (abs) {
    output->append("|");
  }
}

void print_dstreg(
    Output* output, uint32_t num, uint32_t mask, uint32_t dst_exp) {
  output->append("%s%u", dst_exp ? "export" : "R", num);
  if (mask != 0xf) {
    output->append(".");
    for (int i = 0; i < 4; i++) {
      output->append("%c", (mask & 0x1) ? chan_names[i] : '_');
      mask >>= 1;
    }
  }
}

void print_export_comment(
    Output* output, uint32_t num, XE_GPU_SHADER_TYPE type) {
  const char *name = NULL;
  switch (type) {
  case XE_GPU_SHADER_TYPE_VERTEX:
    switch (num) {
    case 62: name = "gl_Position";  break;
    case 63: name = "gl_PointSize"; break;
    }
    break;
  case XE_GPU_SHADER_TYPE_PIXEL:
    switch (num) {
    case 0:  name = "gl_FragColor"; break;
    }
    break;
  }
  /* if we had a symbol table here, we could look
   * up the name of the varying..
   */
  if (name) {
    output->append("\t; %s", name);
  }
}

struct {
  uint32_t num_srcs;
  const char *name;
} vector_instructions[0x20] = {
#define INSTR(opc, num_srcs) { num_srcs, #opc }
    INSTR(ADDv, 2), // 0
    INSTR(MULv, 2), // 1
    INSTR(MAXv, 2), // 2
    INSTR(MINv, 2), // 3
    INSTR(SETEv, 2), // 4
    INSTR(SETGTv, 2), // 5
    INSTR(SETGTEv, 2), // 6
    INSTR(SETNEv, 2), // 7
    INSTR(FRACv, 1), // 8
    INSTR(TRUNCv, 1), // 9
    INSTR(FLOORv, 1), // 10
    INSTR(MULADDv, 3), // 111
    INSTR(CNDEv, 3), // 12
    INSTR(CNDGTEv, 3), // 13
    INSTR(CNDGTv, 3), // 14
    INSTR(DOT4v, 2), // 15
    INSTR(DOT3v, 2), // 16
    INSTR(DOT2ADDv, 3),  // 17 -- ???
    INSTR(CUBEv, 2), // 18
    INSTR(MAX4v, 1), // 19
    INSTR(PRED_SETE_PUSHv, 2), // 20
    INSTR(PRED_SETNE_PUSHv, 2), // 21
    INSTR(PRED_SETGT_PUSHv, 2), // 22
    INSTR(PRED_SETGTE_PUSHv, 2), // 23
    INSTR(KILLEv, 2), // 24
    INSTR(KILLGTv, 2), // 25
    INSTR(KILLGTEv, 2), // 26
    INSTR(KILLNEv, 2), // 27
    INSTR(DSTv, 2), // 28
    INSTR(MOVAv, 1), // 29
}, scalar_instructions[0x40] = {
    INSTR(ADDs, 1), // 0
    INSTR(ADD_PREVs, 1), // 1
    INSTR(MULs, 1), // 2
    INSTR(MUL_PREVs, 1), // 3
    INSTR(MUL_PREV2s, 1), // 4
    INSTR(MAXs, 1), // 5
    INSTR(MINs, 1), // 6
    INSTR(SETEs, 1), // 7
    INSTR(SETGTs, 1), // 8
    INSTR(SETGTEs, 1), // 9
    INSTR(SETNEs, 1), // 10
    INSTR(FRACs, 1), // 11
    INSTR(TRUNCs, 1), // 12
    INSTR(FLOORs, 1), // 13
    INSTR(EXP_IEEE, 1), // 14
    INSTR(LOG_CLAMP, 1), // 15
    INSTR(LOG_IEEE, 1), // 16
    INSTR(RECIP_CLAMP, 1), // 17
    INSTR(RECIP_FF, 1), // 18
    INSTR(RECIP_IEEE, 1), // 19
    INSTR(RECIPSQ_CLAMP, 1), // 20
    INSTR(RECIPSQ_FF, 1), // 21
    INSTR(RECIPSQ_IEEE, 1), // 22
    INSTR(MOVAs, 1), // 23
    INSTR(MOVA_FLOORs, 1), // 24
    INSTR(SUBs, 1), // 25
    INSTR(SUB_PREVs, 1), // 26
    INSTR(PRED_SETEs, 1), // 27
    INSTR(PRED_SETNEs, 1), // 28
    INSTR(PRED_SETGTs, 1), // 29
    INSTR(PRED_SETGTEs, 1), // 30
    INSTR(PRED_SET_INVs, 1), // 31
    INSTR(PRED_SET_POPs, 1), // 32
    INSTR(PRED_SET_CLRs, 1), // 33
    INSTR(PRED_SET_RESTOREs, 1), // 34
    INSTR(KILLEs, 1), // 35
    INSTR(KILLGTs, 1), // 36
    INSTR(KILLGTEs, 1), // 37
    INSTR(KILLNEs, 1), // 38
    INSTR(KILLONEs, 1), // 39
    INSTR(SQRT_IEEE, 1), // 40
    {0, 0},
    INSTR(MUL_CONST_0, 2), // 42
    INSTR(MUL_CONST_1, 2), // 43
    INSTR(ADD_CONST_0, 2), // 44
    INSTR(ADD_CONST_1, 2), // 45
    INSTR(SUB_CONST_0, 2), // 46
    INSTR(SUB_CONST_1, 2), // 47
    INSTR(SIN, 1), // 48
    INSTR(COS, 1), // 49
    INSTR(RETAIN_PREV, 1), // 50
#undef INSTR
};

int disasm_alu(
    Output* output, const uint32_t* dwords, uint32_t alu_off,
    int level, int sync, XE_GPU_SHADER_TYPE type) {
  const instr_alu_t* alu = (const instr_alu_t*)dwords;

  output->append("%s", levels[level]);
  output->append("%02x: %08x %08x %08x\t", alu_off,
          dwords[0], dwords[1], dwords[2]);

  output->append("   %sALU:\t", sync ? "(S)" : "   ");

  if (!alu->scalar_write_mask && !alu->vector_write_mask) {
    output->append("   <nop>\n");
  }

  if (alu->vector_write_mask) {
    output->append("%s", vector_instructions[alu->vector_opc].name);

    if (alu->pred_select & 0x2) {
      // seems to work similar to conditional execution in ARM instruction
      // set, so let's use a similar syntax for now:
      output->append((alu->pred_select & 0x1) ? "EQ" : "NE");
    }

    output->append("\t");

    print_dstreg(output,
                 alu->vector_dest, alu->vector_write_mask, alu->export_data);
    output->append(" = ");
    if (vector_instructions[alu->vector_opc].num_srcs == 3) {
      print_srcreg(output,
                   alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                   alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(", ");
    }
    print_srcreg(output,
                 alu->src1_reg, alu->src1_sel, alu->src1_swiz,
                 alu->src1_reg_negate, alu->src1_reg_abs);
    if (vector_instructions[alu->vector_opc].num_srcs > 1) {
      output->append(", ");
      print_srcreg(output,
                   alu->src2_reg, alu->src2_sel, alu->src2_swiz,
                   alu->src2_reg_negate, alu->src2_reg_abs);
    }

    if (alu->vector_clamp) {
      output->append(" CLAMP");
    }

    if (alu->export_data) {
      print_export_comment(output, alu->vector_dest, type);
    }

    output->append("\n");
  }

  if (alu->scalar_write_mask || !alu->vector_write_mask) {
    // 2nd optional scalar op:

    if (alu->vector_write_mask) {
      output->append("%s", levels[level]);
      output->append("                          \t\t\t\t\t\t    \t");
    }

    if (scalar_instructions[alu->scalar_opc].name) {
      output->append("%s\t", scalar_instructions[alu->scalar_opc].name);
    } else {
      output->append("OP(%u)\t", alu->scalar_opc);
    }

    print_dstreg(output,
                 alu->scalar_dest, alu->scalar_write_mask, alu->export_data);
    output->append(" = ");
    if (scalar_instructions[alu->scalar_opc].num_srcs == 2) {
      // MUL/ADD/etc
      // Clever, CONST_0 and CONST_1 are just an extra storage bit.
      // ADD_CONST_0 dest, [const], [reg]
      uint32_t src3_swiz = alu->src3_swiz & ~0x3C;
      uint32_t swiz_a = ((src3_swiz >> 6) - 1) & 0x3;
      uint32_t swiz_b = (src3_swiz & 0x3);
      print_srcreg(output,
                   alu->src3_reg, 0, 0,
                   alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(".%c", chan_names[swiz_a]);
      output->append(", ");
      uint32_t reg2 = (alu->scalar_opc & 1) | (alu->src3_swiz & 0x3C) | (alu->src3_sel << 1);
      print_srcreg(output,
                   reg2, 1, 0,
                   alu->src3_reg_negate, alu->src3_reg_abs);
      output->append(".%c", chan_names[swiz_b]);
    } else {
      print_srcreg(output,
                   alu->src3_reg, alu->src3_sel, alu->src3_swiz,
                   alu->src3_reg_negate, alu->src3_reg_abs);
    }
    if (alu->scalar_clamp) {
      output->append(" CLAMP");
    }
    if (alu->export_data) {
      print_export_comment(output, alu->scalar_dest, type);
    }
    output->append("\n");
  }

  return 0;
}

struct {
  const char *name;
} fetch_types[0xff] = {
#define TYPE(id) { #id }
    TYPE(FMT_1_REVERSE), // 0
    {0},
    TYPE(FMT_8), // 2
    {0},
    {0},
    {0},
    TYPE(FMT_8_8_8_8), // 6
    TYPE(FMT_2_10_10_10), // 7
    {0},
    {0},
    TYPE(FMT_8_8), // 10
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
    TYPE(FMT_16), // 24
    TYPE(FMT_16_16), // 25
    TYPE(FMT_16_16_16_16), // 26
    {0},
    {0},
    {0},
    {0},
    {0},
    {0},
    TYPE(FMT_32), // 33
    TYPE(FMT_32_32), // 34
    TYPE(FMT_32_32_32_32), // 35
    TYPE(FMT_32_FLOAT), // 36
    TYPE(FMT_32_32_FLOAT), // 37
    TYPE(FMT_32_32_32_32_FLOAT), // 38
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
    TYPE(FMT_32_32_32_FLOAT), // 57
#undef TYPE
};

void print_fetch_dst(Output* output, uint32_t dst_reg, uint32_t dst_swiz) {
  output->append("\tR%u.", dst_reg);
  for (int i = 0; i < 4; i++) {
    output->append("%c", chan_names[dst_swiz & 0x7]);
    dst_swiz >>= 3;
  }
}

void print_fetch_vtx(Output* output, const instr_fetch_t* fetch) {
  const instr_fetch_vtx_t* vtx = &fetch->vtx;

  if (vtx->pred_select) {
    // seems to work similar to conditional execution in ARM instruction
    // set, so let's use a similar syntax for now:
    output->append(vtx->pred_condition ? "EQ" : "NE");
  }

  print_fetch_dst(output, vtx->dst_reg, vtx->dst_swiz);
  output->append(" = R%u.", vtx->src_reg);
  output->append("%c", chan_names[vtx->src_swiz & 0x3]);
  if (fetch_types[vtx->format].name) {
    output->append(" %s", fetch_types[vtx->format].name);
  } else  {
    output->append(" TYPE(0x%x)", vtx->format);
  }
  output->append(" %s", vtx->format_comp_all ? "SIGNED" : "UNSIGNED");
  if (!vtx->num_format_all) {
    output->append(" NORMALIZED");
  }
  output->append(" STRIDE(%u)", vtx->stride);
  if (vtx->offset) {
    output->append(" OFFSET(%u)", vtx->offset);
  }
  output->append(" CONST(%u, %u)", vtx->const_index, vtx->const_index_sel);
  if (1) {
    // XXX
    output->append(" src_reg_am=%u", vtx->src_reg_am);
    output->append(" dst_reg_am=%u", vtx->dst_reg_am);
    output->append(" num_format_all=%u", vtx->num_format_all);
    output->append(" signed_rf_mode_all=%u", vtx->signed_rf_mode_all);
    output->append(" exp_adjust_all=%u", vtx->exp_adjust_all);
  }
}

void print_fetch_tex(Output* output, const instr_fetch_t* fetch) {
  static const char *filter[] = {
    "POINT",    // TEX_FILTER_POINT
    "LINEAR",   // TEX_FILTER_LINEAR
    "BASEMAP",  // TEX_FILTER_BASEMAP
  };
  static const char *aniso_filter[] = {
    "DISABLED", // ANISO_FILTER_DISABLED
    "MAX_1_1",  // ANISO_FILTER_MAX_1_1
    "MAX_2_1",  // ANISO_FILTER_MAX_2_1
    "MAX_4_1",  // ANISO_FILTER_MAX_4_1
    "MAX_8_1",  // ANISO_FILTER_MAX_8_1
    "MAX_16_1", // ANISO_FILTER_MAX_16_1
  };
  static const char *arbitrary_filter[] = {
    "2x4_SYM",  // ARBITRARY_FILTER_2X4_SYM
    "2x4_ASYM", // ARBITRARY_FILTER_2X4_ASYM
    "4x2_SYM",  // ARBITRARY_FILTER_4X2_SYM
    "4x2_ASYM", // ARBITRARY_FILTER_4X2_ASYM
    "4x4_SYM",  // ARBITRARY_FILTER_4X4_SYM
    "4x4_ASYM", // ARBITRARY_FILTER_4X4_ASYM
  };
  static const char *sample_loc[] = {
    "CENTROID", // SAMPLE_CENTROID
    "CENTER",   // SAMPLE_CENTER
  };
  const instr_fetch_tex_t* tex = &fetch->tex;
  uint32_t src_swiz = tex->src_swiz;

  if (tex->pred_select) {
    // seems to work similar to conditional execution in ARM instruction
    // set, so let's use a similar syntax for now:
    output->append(tex->pred_condition ? "EQ" : "NE");
  }

  print_fetch_dst(output, tex->dst_reg, tex->dst_swiz);
  output->append(" = R%u.", tex->src_reg);
  for (int i = 0; i < 3; i++) {
    output->append("%c", chan_names[src_swiz & 0x3]);
    src_swiz >>= 2;
  }
  output->append(" CONST(%u)", tex->const_idx);
  if (tex->fetch_valid_only) {
    output->append(" VALID_ONLY");
  }
  if (tex->tx_coord_denorm) {
    output->append(" DENORM");
  }
  if (tex->mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MAG(%s)", filter[tex->mag_filter]);
  }
  if (tex->min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MIN(%s)", filter[tex->min_filter]);
  }
  if (tex->mip_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" MIP(%s)", filter[tex->mip_filter]);
  }
  if (tex->aniso_filter != ANISO_FILTER_USE_FETCH_CONST) {
    output->append(" ANISO(%s)", aniso_filter[tex->aniso_filter]);
  }
  if (tex->arbitrary_filter != ARBITRARY_FILTER_USE_FETCH_CONST) {
    output->append(" ARBITRARY(%s)", arbitrary_filter[tex->arbitrary_filter]);
  }
  if (tex->vol_mag_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" VOL_MAG(%s)", filter[tex->vol_mag_filter]);
  }
  if (tex->vol_min_filter != TEX_FILTER_USE_FETCH_CONST) {
    output->append(" VOL_MIN(%s)", filter[tex->vol_min_filter]);
  }
  if (!tex->use_comp_lod) {
    output->append(" LOD(%u)", tex->use_comp_lod);
    output->append(" LOD_BIAS(%u)", tex->lod_bias);
  }
  if (tex->use_reg_lod) {
    output->append(" REG_LOD(%u)", tex->use_reg_lod);
  }
  if (tex->use_reg_gradients) {
    output->append(" USE_REG_GRADIENTS");
  }
  output->append(" LOCATION(%s)", sample_loc[tex->sample_location]);
  if (tex->offset_x || tex->offset_y || tex->offset_z) {
    output->append(" OFFSET(%u,%u,%u)", tex->offset_x, tex->offset_y, tex->offset_z);
  }
}

struct {
  const char *name;
  void (*fxn)(Output* output, const instr_fetch_t* cf);
} fetch_instructions[] = {
#define INSTR(opc, name, fxn) { name, fxn }
    INSTR(VTX_FETCH, "VERTEX", print_fetch_vtx), // 0
    INSTR(TEX_FETCH, "SAMPLE", print_fetch_tex), // 1
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
    INSTR(TEX_GET_BORDER_COLOR_FRAC, "?", print_fetch_tex), // 16
    INSTR(TEX_GET_COMP_TEX_LOD, "?", print_fetch_tex), // 17
    INSTR(TEX_GET_GRADIENTS, "?", print_fetch_tex), // 18
    INSTR(TEX_GET_WEIGHTS, "?", print_fetch_tex), // 19
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    INSTR(TEX_SET_TEX_LOD, "SET_TEX_LOD", print_fetch_tex), // 24
    INSTR(TEX_SET_GRADIENTS_H, "?", print_fetch_tex), // 25
    INSTR(TEX_SET_GRADIENTS_V, "?", print_fetch_tex), // 26
    INSTR(TEX_RESERVED_4, "?", print_fetch_tex), // 27
#undef INSTR
};

int disasm_fetch(
    Output* output, const uint32_t* dwords, uint32_t alu_off, int level, int sync) {
  const instr_fetch_t* fetch = (const instr_fetch_t*)dwords;

  output->append("%s", levels[level]);
  output->append("%02x: %08x %08x %08x\t", alu_off,
                 dwords[0], dwords[1], dwords[2]);

  output->append("   %sFETCH:\t", sync ? "(S)" : "   ");
  if (fetch_instructions[fetch->opc].fxn) {
    output->append("%s", fetch_instructions[fetch->opc].name);
    fetch_instructions[fetch->opc].fxn(output, fetch);
  } else {
    output->append("???");
  }
  output->append("\n");

  return 0;
}

void print_cf_nop(Output* output, const instr_cf_t* cf) {
}

void print_cf_exec(Output* output, const instr_cf_t* cf) {
  output->append(" ADDR(0x%x) CNT(0x%x)", cf->exec.address, cf->exec.count);
  if (cf->exec.yeild) {
    output->append(" YIELD");
  }
  uint8_t vc = cf->exec.vc_hi | (cf->exec.vc_lo << 2);
  if (vc) {
    output->append(" VC(0x%x)", vc);
  }
  if (cf->exec.bool_addr) {
    output->append(" BOOL_ADDR(0x%x)", cf->exec.bool_addr);
  }
  if (cf->exec.address_mode == ABSOLUTE_ADDR) {
    output->append(" ABSOLUTE_ADDR");
  }
  if (cf->is_cond_exec()) {
    output->append(" COND(%d)", cf->exec.condition);
  }
}

void print_cf_loop(Output* output, const instr_cf_t* cf) {
  output->append(" ADDR(0x%x) LOOP_ID(%d)", cf->loop.address, cf->loop.loop_id);
  if (cf->loop.address_mode == ABSOLUTE_ADDR) {
    output->append(" ABSOLUTE_ADDR");
  }
}

void print_cf_jmp_call(Output* output, const instr_cf_t* cf) {
  output->append(" ADDR(0x%x) DIR(%d)", cf->jmp_call.address, cf->jmp_call.direction);
  if (cf->jmp_call.force_call) {
    output->append(" FORCE_CALL");
  }
  if (cf->jmp_call.predicated_jmp) {
    output->append(" COND(%d)", cf->jmp_call.condition);
  }
  if (cf->jmp_call.bool_addr) {
    output->append(" BOOL_ADDR(0x%x)", cf->jmp_call.bool_addr);
  }
  if (cf->jmp_call.address_mode == ABSOLUTE_ADDR) {
    output->append(" ABSOLUTE_ADDR");
  }
}

void print_cf_alloc(Output* output, const instr_cf_t* cf) {
  static const char *bufname[] = {
    "NO ALLOC",     // SQ_NO_ALLOC
    "POSITION",     // SQ_POSITION
    "PARAM/PIXEL",  // SQ_PARAMETER_PIXEL
    "MEMORY",       // SQ_MEMORY
  };
  output->append(" %s SIZE(0x%x)", bufname[cf->alloc.buffer_select], cf->alloc.size);
  if (cf->alloc.no_serial) {
    output->append(" NO_SERIAL");
  }
  if (cf->alloc.alloc_mode) {
    // ???
    output->append(" ALLOC_MODE");
  }
}

struct {
  const char *name;
  void (*fxn)(Output* output, const instr_cf_t* cf);
} cf_instructions[] = {
#define INSTR(opc, fxn) { #opc, fxn }
    INSTR(NOP, print_cf_nop),
    INSTR(EXEC, print_cf_exec),
    INSTR(EXEC_END, print_cf_exec),
    INSTR(COND_EXEC, print_cf_exec),
    INSTR(COND_EXEC_END, print_cf_exec),
    INSTR(COND_PRED_EXEC, print_cf_exec),
    INSTR(COND_PRED_EXEC_END, print_cf_exec),
    INSTR(LOOP_START, print_cf_loop),
    INSTR(LOOP_END, print_cf_loop),
    INSTR(COND_CALL, print_cf_jmp_call),
    INSTR(RETURN, print_cf_jmp_call),
    INSTR(COND_JMP, print_cf_jmp_call),
    INSTR(ALLOC, print_cf_alloc),
    INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),
    INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),
    INSTR(MARK_VS_FETCH_DONE, print_cf_nop),  // ??
#undef INSTR
};

static void print_cf(Output* output, const instr_cf_t* cf, int level) {
  output->append("%s", levels[level]);

  const uint16_t* words = (uint16_t*)cf;
  output->append("    %04x %04x %04x            \t",
                 words[0], words[1], words[2]);

  output->append("%s", cf_instructions[cf->opc].name);
  cf_instructions[cf->opc].fxn(output, cf);
  output->append("\n");
}

/*
 * The adreno shader microcode consists of two parts:
 *   1) A CF (control-flow) program, at the header of the compiled shader,
 *      which refers to ALU/FETCH instructions that follow it by address.
 *   2) ALU and FETCH instructions
 */
void disasm_exec(
    Output* output, const uint32_t* dwords, size_t dword_count,
    int level, XE_GPU_SHADER_TYPE type, const instr_cf_t* cf) {
  uint32_t sequence = cf->exec.serialize;
  for (uint32_t i = 0; i < cf->exec.count; i++) {
    uint32_t alu_off = (cf->exec.address + i);
    if (sequence & 0x1) {
      disasm_fetch(output, dwords + alu_off * 3,
                   alu_off, level, sequence & 0x2);
    } else {
      disasm_alu(output, dwords + alu_off * 3,
                 alu_off, level, sequence & 0x2, type);
    }
    sequence >>= 2;
  }
}

}  // anonymous namespace


char* xenos::DisassembleShader(
    XE_GPU_SHADER_TYPE type,
    const uint32_t* dwords, size_t dword_count) {
  Output* output = new Output();

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
    print_cf(output, &cfa, 0);
    if (cfa.is_exec()) {
      disasm_exec(output, dwords, dword_count, 0, type, &cfa);
    }
    print_cf(output, &cfb, 0);
    if (cfb.is_exec()) {
      disasm_exec(output, dwords, dword_count, 0, type, &cfb);
    }
    if (cfa.opc == EXEC_END || cfb.opc == EXEC_END) {
      break;
    }
  }

  char* result = xestrdupa(output->buffer);
  delete output;
  return result;
}
