/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_disasm.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"

namespace xe {
namespace cpu {
namespace frontend {

void Disasm_0(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s ???", i->type->name);
}

void Disasm__(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s", i->type->name);
}

void Disasm_X_FRT_FRB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s f%d, f%d", i->X.Rc ? -7 : -8, i->type->name,
                    i->X.Rc ? "." : "", i->X.RT, i->X.RB);
}
void Disasm_A_FRT_FRB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s f%d, f%d", i->A.Rc ? -7 : -8, i->type->name,
                    i->A.Rc ? "." : "", i->A.FRT, i->A.FRB);
}
void Disasm_A_FRT_FRA_FRB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s f%d, f%d, f%d", i->A.Rc ? -7 : -8, i->type->name,
                    i->A.Rc ? "." : "", i->A.FRT, i->A.FRA, i->A.FRB);
}
void Disasm_A_FRT_FRA_FRB_FRC(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s f%d, f%d, f%d, f%d", i->A.Rc ? -7 : -8,
                    i->type->name, i->A.Rc ? "." : "", i->A.FRT, i->A.FRA,
                    i->A.FRB, i->A.FRC);
}
void Disasm_X_RT_RA_RB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, r%d, r%d", i->type->name, i->X.RT, i->X.RA,
                    i->X.RB);
}
void Disasm_X_RT_RA0_RB(InstrData* i, StringBuffer* str) {
  if (i->X.RA) {
    str->AppendFormat("%-8s r%d, r%d, r%d", i->type->name, i->X.RT, i->X.RA,
                      i->X.RB);
  } else {
    str->AppendFormat("%-8s r%d, 0, r%d", i->type->name, i->X.RT, i->X.RB);
  }
}
void Disasm_X_FRT_RA_RB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s f%d, r%d, r%d", i->type->name, i->X.RT, i->X.RA,
                    i->X.RB);
}
void Disasm_X_FRT_RA0_RB(InstrData* i, StringBuffer* str) {
  if (i->X.RA) {
    str->AppendFormat("%-8s f%d, r%d, r%d", i->type->name, i->X.RT, i->X.RA,
                      i->X.RB);
  } else {
    str->AppendFormat("%-8s f%d, 0, r%d", i->type->name, i->X.RT, i->X.RB);
  }
}
void Disasm_D_RT_RA_I(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, r%d, %d", i->type->name, i->D.RT, i->D.RA,
                    (int32_t)(int16_t)XEEXTS16(i->D.DS));
}
void Disasm_D_RT_RA0_I(InstrData* i, StringBuffer* str) {
  if (i->D.RA) {
    str->AppendFormat("%-8s r%d, r%d, %d", i->type->name, i->D.RT, i->D.RA,
                      (int32_t)(int16_t)XEEXTS16(i->D.DS));
  } else {
    str->AppendFormat("%-8s r%d, 0, %d", i->type->name, i->D.RT,
                      (int32_t)(int16_t)XEEXTS16(i->D.DS));
  }
}
void Disasm_D_FRT_RA_I(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s f%d, r%d, %d", i->type->name, i->D.RT, i->D.RA,
                    (int32_t)(int16_t)XEEXTS16(i->D.DS));
}
void Disasm_D_FRT_RA0_I(InstrData* i, StringBuffer* str) {
  if (i->D.RA) {
    str->AppendFormat("%-8s f%d, r%d, %d", i->type->name, i->D.RT, i->D.RA,
                      (int32_t)(int16_t)XEEXTS16(i->D.DS));
  } else {
    str->AppendFormat("%-8s f%d, 0, %d", i->type->name, i->D.RT,
                      (int32_t)(int16_t)XEEXTS16(i->D.DS));
  }
}
void Disasm_DS_RT_RA_I(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, r%d, %d", i->type->name, i->DS.RT, i->DS.RA,
                    (int32_t)(int16_t)XEEXTS16(i->DS.DS << 2));
}
void Disasm_DS_RT_RA0_I(InstrData* i, StringBuffer* str) {
  if (i->DS.RA) {
    str->AppendFormat("%-8s r%d, r%d, %d", i->type->name, i->DS.RT, i->DS.RA,
                      (int32_t)(int16_t)XEEXTS16(i->DS.DS << 2));
  } else {
    str->AppendFormat("%-8s r%d, 0, %d", i->type->name, i->DS.RT,
                      (int32_t)(int16_t)XEEXTS16(i->DS.DS << 2));
  }
}
void Disasm_D_RA(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d", i->type->name, i->D.RA);
}
void Disasm_X_RA_RB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, r%d", i->type->name, i->X.RA, i->X.RB);
}
void Disasm_XO_RT_RA_RB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s%s r%d, r%d, r%d", i->XO.Rc ? -7 : -8, i->type->name,
                    i->XO.OE ? "o" : "", i->XO.Rc ? "." : "", i->XO.RT,
                    i->XO.RA, i->XO.RB);
}
void Disasm_XO_RT_RA(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s%s r%d, r%d", i->XO.Rc ? -7 : -8, i->type->name,
                    i->XO.OE ? "o" : "", i->XO.Rc ? "." : "", i->XO.RT,
                    i->XO.RA);
}
void Disasm_X_RA_RT_RB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d, r%d", i->X.Rc ? -7 : -8, i->type->name,
                    i->X.Rc ? "." : "", i->X.RA, i->X.RT, i->X.RB);
}
void Disasm_D_RA_RT_I(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-7s. r%d, r%d, %.4Xh", i->type->name, i->D.RA, i->D.RT,
                    i->D.DS);
}
void Disasm_X_RA_RT(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d", i->X.Rc ? -7 : -8, i->type->name,
                    i->X.Rc ? "." : "", i->X.RA, i->X.RT);
}

#define OP(x) ((((uint32_t)(x)) & 0x3f) << 26)
#define VX128(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x3d0))
#define VX128_1(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f3))
#define VX128_2(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x210))
#define VX128_3(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f0))
#define VX128_4(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x730))
#define VX128_5(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x10))
#define VX128_P(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x630))

#define VX128_VD128 (i->VX128.VD128l | (i->VX128.VD128h << 5))
#define VX128_VA128 \
  (i->VX128.VA128l | (i->VX128.VA128h << 5) | (i->VX128.VA128H << 6))
#define VX128_VB128 (i->VX128.VB128l | (i->VX128.VB128h << 5))
#define VX128_1_VD128 (i->VX128_1.VD128l | (i->VX128_1.VD128h << 5))
#define VX128_2_VD128 (i->VX128_2.VD128l | (i->VX128_2.VD128h << 5))
#define VX128_2_VA128 \
  (i->VX128_2.VA128l | (i->VX128_2.VA128h << 5) | (i->VX128_2.VA128H << 6))
#define VX128_2_VB128 (i->VX128_2.VB128l | (i->VX128_2.VB128h << 5))
#define VX128_2_VC (i->VX128_2.VC)
#define VX128_3_VD128 (i->VX128_3.VD128l | (i->VX128_3.VD128h << 5))
#define VX128_3_VB128 (i->VX128_3.VB128l | (i->VX128_3.VB128h << 5))
#define VX128_3_IMM (i->VX128_3.IMM)
#define VX128_4_VD128 (i->VX128_4.VD128l | (i->VX128_4.VD128h << 5))
#define VX128_4_VB128 (i->VX128_4.VB128l | (i->VX128_4.VB128h << 5))
#define VX128_5_VD128 (i->VX128_5.VD128l | (i->VX128_5.VD128h << 5))
#define VX128_5_VA128 \
  (i->VX128_5.VA128l | (i->VX128_5.VA128h << 5)) | (i->VX128_5.VA128H << 6)
#define VX128_5_VB128 (i->VX128_5.VB128l | (i->VX128_5.VB128h << 5))
#define VX128_5_SH (i->VX128_5.SH)
#define VX128_R_VD128 (i->VX128_R.VD128l | (i->VX128_R.VD128h << 5))
#define VX128_R_VA128 \
  (i->VX128_R.VA128l | (i->VX128_R.VA128h << 5) | (i->VX128_R.VA128H << 6))
#define VX128_R_VB128 (i->VX128_R.VB128l | (i->VX128_R.VB128h << 5))

void Disasm_X_VX_RA0_RB(InstrData* i, StringBuffer* str) {
  if (i->X.RA) {
    str->AppendFormat("%-8s v%d, r%d, r%d", i->type->name, i->X.RT, i->X.RA,
                      i->X.RB);
  } else {
    str->AppendFormat("%-8s v%d, 0, r%d", i->type->name, i->X.RT, i->X.RB);
  }
}
void Disasm_VX1281_VD_RA0_RB(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_1_VD128;
  if (i->VX128_1.RA) {
    str->AppendFormat("%-8s v%d, r%d, r%d", i->type->name, vd, i->VX128_1.RA,
                      i->VX128_1.RB);
  } else {
    str->AppendFormat("%-8s v%d, 0, r%d", i->type->name, vd, i->VX128_1.RB);
  }
}
void Disasm_VX1283_VD_VB(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_3_VD128;
  const uint32_t vb = VX128_3_VB128;
  str->AppendFormat("%-8s v%d, v%d", i->type->name, vd, vb);
}
void Disasm_VX1283_VD_VB_I(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_VD128;
  const uint32_t va = VX128_VA128;
  const uint32_t uimm = i->VX128_3.IMM;
  str->AppendFormat("%-8s v%d, v%d, %.2Xh", i->type->name, vd, va, uimm);
}
void Disasm_VX_VD_VA_VB(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s v%d, v%d, v%d", i->type->name, i->VX.VD, i->VX.VA,
                    i->VX.VB);
}
void Disasm_VX128_VD_VA_VB(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_VD128;
  const uint32_t va = VX128_VA128;
  const uint32_t vb = VX128_VB128;
  str->AppendFormat("%-8s v%d, v%d, v%d", i->type->name, vd, va, vb);
}
void Disasm_VX128_VD_VA_VD_VB(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_VD128;
  const uint32_t va = VX128_VA128;
  const uint32_t vb = VX128_VB128;
  str->AppendFormat("%-8s v%d, v%d, v%d, v%d", i->type->name, vd, va, vd, vb);
}
void Disasm_VX1282_VD_VA_VB_VC(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_2_VD128;
  const uint32_t va = VX128_2_VA128;
  const uint32_t vb = VX128_2_VB128;
  const uint32_t vc = i->VX128_2.VC;
  str->AppendFormat("%-8s v%d, v%d, v%d, v%d", i->type->name, vd, va, vb, vc);
}
void Disasm_VXA_VD_VA_VB_VC(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s v%d, v%d, v%d, v%d", i->type->name, i->VXA.VD,
                    i->VXA.VA, i->VXA.VB, i->VXA.VC);
}

void Disasm_sync(InstrData* i, StringBuffer* str) {
  const char* name;
  int L = i->X.RT & 3;
  switch (L) {
    case 0:
      name = "hwsync";
      break;
    case 1:
      name = "lwsync";
      break;
    default:
    case 2:
    case 3:
      name = "sync";
      break;
  }
  str->AppendFormat("%-8s %.2X", name, L);
}

void Disasm_dcbf(InstrData* i, StringBuffer* str) {
  const char* name;
  switch (i->X.RT & 3) {
    case 0:
      name = "dcbf";
      break;
    case 1:
      name = "dcbfl";
      break;
    case 2:
      name = "dcbf.RESERVED";
      break;
    case 3:
      name = "dcbflp";
      break;
    default:
      name = "dcbf.??";
      break;
  }
  str->AppendFormat("%-8s r%d, r%d", name, i->X.RA, i->X.RB);
}

void Disasm_dcbz(InstrData* i, StringBuffer* str) {
  // or dcbz128 0x7C2007EC
  if (i->X.RA) {
    str->AppendFormat("%-8s r%d, r%d", i->type->name, i->X.RA, i->X.RB);
  } else {
    str->AppendFormat("%-8s 0, r%d", i->type->name, i->X.RB);
  }
}

void Disasm_fcmp(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s cr%d, f%d, f%d", i->type->name, i->X.RT >> 2, i->X.RA,
                    i->X.RB);
}

void Disasm_mffsx(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s f%d, FPSCR", i->X.Rc ? -7 : -8, i->type->name,
                    i->X.Rc ? "." : "", i->X.RT);
}

void Disasm_bx(InstrData* i, StringBuffer* str) {
  const char* name = i->I.LK ? "bl" : "b";
  uint32_t nia;
  if (i->I.AA) {
    nia = (uint32_t)XEEXTS26(i->I.LI << 2);
  } else {
    nia = (uint32_t)(i->address + XEEXTS26(i->I.LI << 2));
  }
  str->AppendFormat("%-8s %.8X", name, nia);
  // TODO(benvanik): resolve target name?
}
void Disasm_bcx(InstrData* i, StringBuffer* str) {
  const char* s0 = i->B.LK ? "lr, " : "";
  const char* s1;
  if (!select_bits(i->B.BO, 2, 2)) {
    s1 = "ctr, ";
  } else {
    s1 = "";
  }
  char s2[8] = {0};
  if (!select_bits(i->B.BO, 4, 4)) {
    snprintf(s2, xe::countof(s2), "cr%d, ", i->B.BI >> 2);
  }
  uint32_t nia;
  if (i->B.AA) {
    nia = (uint32_t)XEEXTS16(i->B.BD << 2);
  } else {
    nia = (uint32_t)(i->address + XEEXTS16(i->B.BD << 2));
  }
  str->AppendFormat("%-8s %s%s%s%.8X", i->type->name, s0, s1, s2, nia);
  // TODO(benvanik): resolve target name?
}
void Disasm_bcctrx(InstrData* i, StringBuffer* str) {
  // TODO(benvanik): mnemonics
  const char* s0 = i->XL.LK ? "lr, " : "";
  char s2[8] = {0};
  if (!select_bits(i->XL.BO, 4, 4)) {
    snprintf(s2, xe::countof(s2), "cr%d, ", i->XL.BI >> 2);
  }
  str->AppendFormat("%-8s %s%sctr", i->type->name, s0, s2);
  // TODO(benvanik): resolve target name?
}
void Disasm_bclrx(InstrData* i, StringBuffer* str) {
  const char* name = "bclr";
  if (i->code == 0x4E800020) {
    name = "blr";
  }
  const char* s1;
  if (!select_bits(i->XL.BO, 2, 2)) {
    s1 = "ctr, ";
  } else {
    s1 = "";
  }
  char s2[8] = {0};
  if (!select_bits(i->XL.BO, 4, 4)) {
    snprintf(s2, xe::countof(s2), "cr%d, ", i->XL.BI >> 2);
  }
  str->AppendFormat("%-8s %s%s", name, s1, s2);
}

void Disasm_mfcr(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, cr", i->type->name, i->X.RT);
}
const char* Disasm_spr_name(uint32_t n) {
  const char* reg = "???";
  switch (n) {
    case 1:
      reg = "xer";
      break;
    case 8:
      reg = "lr";
      break;
    case 9:
      reg = "ctr";
      break;
  }
  return reg;
}
void Disasm_mfspr(InstrData* i, StringBuffer* str) {
  const uint32_t n = ((i->XFX.spr & 0x1F) << 5) | ((i->XFX.spr >> 5) & 0x1F);
  const char* reg = Disasm_spr_name(n);
  str->AppendFormat("%-8s r%d, %s", i->type->name, i->XFX.RT, reg);
}
void Disasm_mtspr(InstrData* i, StringBuffer* str) {
  const uint32_t n = ((i->XFX.spr & 0x1F) << 5) | ((i->XFX.spr >> 5) & 0x1F);
  const char* reg = Disasm_spr_name(n);
  str->AppendFormat("%-8s %s, r%d", i->type->name, reg, i->XFX.RT);
}
void Disasm_mftb(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, tb", i->type->name, i->XFX.RT);
}
void Disasm_mfmsr(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d", i->type->name, i->X.RT);
}
void Disasm_mtmsr(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s r%d, %d", i->type->name, i->X.RT,
                    (i->X.RA & 16) ? 1 : 0);
}

void Disasm_cmp(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s cr%d, %.2X, r%d, r%d", i->type->name, i->X.RT >> 2,
                    i->X.RT & 1, i->X.RA, i->X.RB);
}
void Disasm_cmpi(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s cr%d, %.2X, r%d, %d", i->type->name, i->D.RT >> 2,
                    i->D.RT & 1, i->D.RA, XEEXTS16(i->D.DS));
}
void Disasm_cmpli(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s cr%d, %.2X, r%d, %.2X", i->type->name, i->D.RT >> 2,
                    i->D.RT & 1, i->D.RA, XEEXTS16(i->D.DS));
}

void Disasm_rld(InstrData* i, StringBuffer* str) {
  if (i->MD.idx == 0) {
    // XEDISASMR(rldiclx,      0x78000000, MD )
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MD.Rc ? -7 : -8, "rldicl",
                      i->MD.Rc ? "." : "", i->MD.RA, i->MD.RT,
                      (i->MD.SH5 << 5) | i->MD.SH, (i->MD.MB5 << 5) | i->MD.MB);
  } else if (i->MD.idx == 1) {
    // XEDISASMR(rldicrx,      0x78000004, MD )
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MD.Rc ? -7 : -8, "rldicr",
                      i->MD.Rc ? "." : "", i->MD.RA, i->MD.RT,
                      (i->MD.SH5 << 5) | i->MD.SH, (i->MD.MB5 << 5) | i->MD.MB);
  } else if (i->MD.idx == 2) {
    // XEDISASMR(rldicx,       0x78000008, MD )
    uint32_t sh = (i->MD.SH5 << 5) | i->MD.SH;
    uint32_t mb = (i->MD.MB5 << 5) | i->MD.MB;
    const char* name = (mb == 0x3E) ? "sldi" : "rldic";
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MD.Rc ? -7 : -8, name,
                      i->MD.Rc ? "." : "", i->MD.RA, i->MD.RT, sh, mb);
  } else if (i->MDS.idx == 8) {
    // XEDISASMR(rldclx,       0x78000010, MDS)
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MDS.Rc ? -7 : -8, "rldcl",
                      i->MDS.Rc ? "." : "", i->MDS.RA, i->MDS.RT, i->MDS.RB,
                      (i->MDS.MB5 << 5) | i->MDS.MB);
  } else if (i->MDS.idx == 9) {
    // XEDISASMR(rldcrx,       0x78000012, MDS)
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MDS.Rc ? -7 : -8, "rldcr",
                      i->MDS.Rc ? "." : "", i->MDS.RA, i->MDS.RT, i->MDS.RB,
                      (i->MDS.MB5 << 5) | i->MDS.MB);
  } else if (i->MD.idx == 3) {
    // XEDISASMR(rldimix,      0x7800000C, MD )
    str->AppendFormat("%*s%s r%d, r%d, %d, %d", i->MD.Rc ? -7 : -8, "rldimi",
                      i->MD.Rc ? "." : "", i->MD.RA, i->MD.RT,
                      (i->MD.SH5 << 5) | i->MD.SH, (i->MD.MB5 << 5) | i->MD.MB);
  } else {
    assert_always();
  }
}
void Disasm_rlwim(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d, %d, %d, %d", i->M.Rc ? -7 : -8,
                    i->type->name, i->M.Rc ? "." : "", i->M.RA, i->M.RT,
                    i->M.SH, i->M.MB, i->M.ME);
}
void Disasm_rlwnmx(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d, r%d, %d, %d", i->M.Rc ? -7 : -8,
                    i->type->name, i->M.Rc ? "." : "", i->M.RA, i->M.RT,
                    i->M.SH, i->M.MB, i->M.ME);
}
void Disasm_srawix(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d, %d", i->X.Rc ? -7 : -8, i->type->name,
                    i->X.Rc ? "." : "", i->X.RA, i->X.RT, i->X.RB);
}
void Disasm_sradix(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%*s%s r%d, r%d, %d", i->XS.Rc ? -7 : -8, i->type->name,
                    i->XS.Rc ? "." : "", i->XS.RA, i->XS.RT,
                    (i->XS.SH5 << 5) | i->XS.SH);
}

void Disasm_vpermwi128(InstrData* i, StringBuffer* str) {
  const uint32_t vd = i->VX128_P.VD128l | (i->VX128_P.VD128h << 5);
  const uint32_t vb = i->VX128_P.VB128l | (i->VX128_P.VB128h << 5);
  str->AppendFormat("%-8s v%d, v%d, %.2X", i->type->name, vd, vb,
                    i->VX128_P.PERMl | (i->VX128_P.PERMh << 5));
}
void Disasm_vrfin128(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_3_VD128;
  const uint32_t vb = VX128_3_VB128;
  str->AppendFormat("%-8s v%d, v%d", i->type->name, vd, vb);
}
void Disasm_vrlimi128(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_4_VD128;
  const uint32_t vb = VX128_4_VB128;
  str->AppendFormat("%-8s v%d, v%d, %.2X, %.2X", i->type->name, vd, vb,
                    i->VX128_4.IMM, i->VX128_4.z);
}
void Disasm_vsldoi128(InstrData* i, StringBuffer* str) {
  const uint32_t vd = VX128_5_VD128;
  const uint32_t va = VX128_5_VA128;
  const uint32_t vb = VX128_5_VB128;
  const uint32_t sh = i->VX128_5.SH;
  str->AppendFormat("%-8s v%d, v%d, v%d, %.2X", i->type->name, vd, va, vb, sh);
}
void Disasm_vspltb(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s v%d, v%d, %.2X", i->type->name, i->VX.VD, i->VX.VB,
                    i->VX.VA & 0xF);
}
void Disasm_vsplth(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s v%d, v%d, %.2X", i->type->name, i->VX.VD, i->VX.VB,
                    i->VX.VA & 0x7);
}
void Disasm_vspltw(InstrData* i, StringBuffer* str) {
  str->AppendFormat("%-8s v%d, v%d, %.2X", i->type->name, i->VX.VD, i->VX.VB,
                    i->VX.VA);
}
void Disasm_vspltisb(InstrData* i, StringBuffer* str) {
  // 5bit -> 8bit sign extend
  int8_t simm = (i->VX.VA & 0x10) ? (i->VX.VA | 0xF0) : i->VX.VA;
  str->AppendFormat("%-8s v%d, %.2X", i->type->name, i->VX.VD, simm);
}
void Disasm_vspltish(InstrData* i, StringBuffer* str) {
  // 5bit -> 16bit sign extend
  int16_t simm = (i->VX.VA & 0x10) ? (i->VX.VA | 0xFFF0) : i->VX.VA;
  str->AppendFormat("%-8s v%d, %.4X", i->type->name, i->VX.VD, simm);
}
void Disasm_vspltisw(InstrData* i, StringBuffer* str) {
  // 5bit -> 32bit sign extend
  int32_t simm = (i->VX.VA & 0x10) ? (i->VX.VA | 0xFFFFFFF0) : i->VX.VA;
  str->AppendFormat("%-8s v%d, %.8X", i->type->name, i->VX.VD, simm);
}

int DisasmPPC(InstrData* i, StringBuffer* str) {
  if (!i->type) {
    str->Append("???");
  } else {
    i->type->disasm(i, str);
  }
  return 0;
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
