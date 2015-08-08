/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FRONTEND_PPC_INSTR_TABLES_H_
#define XENIA_CPU_FRONTEND_PPC_INSTR_TABLES_H_

#include <cmath>

#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/frontend/ppc_instr.h"

namespace xe {
namespace cpu {
namespace frontend {

void Disasm_0(InstrData* i, StringBuffer* str);
void Disasm__(InstrData* i, StringBuffer* str);
void Disasm_X_FRT_FRB(InstrData* i, StringBuffer* str);
void Disasm_A_FRT_FRB(InstrData* i, StringBuffer* str);
void Disasm_A_FRT_FRA_FRB(InstrData* i, StringBuffer* str);
void Disasm_A_FRT_FRA_FRB_FRC(InstrData* i, StringBuffer* str);
void Disasm_X_RT_RA_RB(InstrData* i, StringBuffer* str);
void Disasm_X_RT_RA0_RB(InstrData* i, StringBuffer* str);
void Disasm_X_FRT_RA_RB(InstrData* i, StringBuffer* str);
void Disasm_X_FRT_RA0_RB(InstrData* i, StringBuffer* str);
void Disasm_D_RT_RA_I(InstrData* i, StringBuffer* str);
void Disasm_D_RT_RA0_I(InstrData* i, StringBuffer* str);
void Disasm_D_FRT_RA_I(InstrData* i, StringBuffer* str);
void Disasm_D_FRT_RA0_I(InstrData* i, StringBuffer* str);
void Disasm_DS_RT_RA_I(InstrData* i, StringBuffer* str);
void Disasm_DS_RT_RA0_I(InstrData* i, StringBuffer* str);
void Disasm_D_RA(InstrData* i, StringBuffer* str);
void Disasm_X_RA_RB(InstrData* i, StringBuffer* str);
void Disasm_XO_RT_RA_RB(InstrData* i, StringBuffer* str);
void Disasm_XO_RT_RA(InstrData* i, StringBuffer* str);
void Disasm_X_RA_RT_RB(InstrData* i, StringBuffer* str);
void Disasm_D_RA_RT_I(InstrData* i, StringBuffer* str);
void Disasm_X_RA_RT(InstrData* i, StringBuffer* str);
void Disasm_X_VX_RA0_RB(InstrData* i, StringBuffer* str);
void Disasm_VX1281_VD_RA0_RB(InstrData* i, StringBuffer* str);
void Disasm_VX1283_VD_VB(InstrData* i, StringBuffer* str);
void Disasm_VX1283_VD_VB_I(InstrData* i, StringBuffer* str);
void Disasm_VX_VD_VA_VB(InstrData* i, StringBuffer* str);
void Disasm_VX128_VD_VA_VB(InstrData* i, StringBuffer* str);
void Disasm_VX128_VD_VA_VD_VB(InstrData* i, StringBuffer* str);
void Disasm_VX1282_VD_VA_VB_VC(InstrData* i, StringBuffer* str);
void Disasm_VXA_VD_VA_VB_VC(InstrData* i, StringBuffer* str);

void Disasm_sync(InstrData* i, StringBuffer* str);
void Disasm_dcbf(InstrData* i, StringBuffer* str);
void Disasm_dcbz(InstrData* i, StringBuffer* str);
void Disasm_fcmp(InstrData* i, StringBuffer* str);

void Disasm_bx(InstrData* i, StringBuffer* str);
void Disasm_bcx(InstrData* i, StringBuffer* str);
void Disasm_bcctrx(InstrData* i, StringBuffer* str);
void Disasm_bclrx(InstrData* i, StringBuffer* str);

void Disasm_mfcr(InstrData* i, StringBuffer* str);
void Disasm_mfspr(InstrData* i, StringBuffer* str);
void Disasm_mtspr(InstrData* i, StringBuffer* str);
void Disasm_mftb(InstrData* i, StringBuffer* str);
void Disasm_mfmsr(InstrData* i, StringBuffer* str);
void Disasm_mtmsr(InstrData* i, StringBuffer* str);

void Disasm_cmp(InstrData* i, StringBuffer* str);
void Disasm_cmpi(InstrData* i, StringBuffer* str);
void Disasm_cmpli(InstrData* i, StringBuffer* str);

void Disasm_rld(InstrData* i, StringBuffer* str);
void Disasm_rlwim(InstrData* i, StringBuffer* str);
void Disasm_rlwnmx(InstrData* i, StringBuffer* str);
void Disasm_srawix(InstrData* i, StringBuffer* str);
void Disasm_sradix(InstrData* i, StringBuffer* str);

void Disasm_vpermwi128(InstrData* i, StringBuffer* str);
void Disasm_vrfin128(InstrData* i, StringBuffer* str);
void Disasm_vrlimi128(InstrData* i, StringBuffer* str);
void Disasm_vsldoi128(InstrData* i, StringBuffer* str);
void Disasm_vspltb(InstrData* i, StringBuffer* str);
void Disasm_vsplth(InstrData* i, StringBuffer* str);
void Disasm_vspltw(InstrData* i, StringBuffer* str);
void Disasm_vspltisb(InstrData* i, StringBuffer* str);
void Disasm_vspltish(InstrData* i, StringBuffer* str);
void Disasm_vspltisw(InstrData* i, StringBuffer* str);

namespace tables {

static InstrType** instr_table_prep(InstrType* unprep, size_t unprep_count,
                                    int a, int b) {
  int prep_count = (int)pow(2.0, b - a + 1);
  InstrType** prep = (InstrType**)calloc(prep_count, sizeof(void*));
  for (int n = 0; n < unprep_count; n++) {
    int ordinal = select_bits(unprep[n].opcode, a, b);
    prep[ordinal] = &unprep[n];
  }
  return prep;
}

static InstrType** instr_table_prep_63(InstrType* unprep, size_t unprep_count,
                                       int a, int b) {
  // Special handling for A format instructions.
  int prep_count = (int)pow(2.0, b - a + 1);
  InstrType** prep = (InstrType**)calloc(prep_count, sizeof(void*));
  for (int n = 0; n < unprep_count; n++) {
    int ordinal = select_bits(unprep[n].opcode, a, b);
    if (unprep[n].format == kXEPPCInstrFormatA) {
      // Must splat this into all of the slots that it could be in.
      for (int m = 0; m < 32; m++) {
        prep[ordinal + (m << 5)] = &unprep[n];
      }
    } else {
      prep[ordinal] = &unprep[n];
    }
  }
  return prep;
}

#define EMPTY(slot) \
  { 0 }
#define INSTRUCTION(name, opcode, format, type, disasm_fn, descr)   \
  {                                                                 \
    opcode, 0, kXEPPCInstrFormat##format, kXEPPCInstrType##type, 0, \
        Disasm_##disasm_fn, #name, 0,                               \
  }
#define FLAG(t) kXEPPCInstrFlag##t

// This table set is constructed from:
//   pem_64bit_v3.0.2005jul15.pdf, A.2
//   PowerISA_V2.06B_V2_PUBLIC.pdf

// Opcode = 4, index = bits 11-0 (6)
static InstrType instr_table_4_unprep[] = {
    // TODO: all of the vector ops
    INSTRUCTION(mfvscr, 0x10000604, VX, General, 0,
                "Move from Vector Status and Control Register"),
    INSTRUCTION(mtvscr, 0x10000644, VX, General, 0,
                "Move to Vector Status and Control Register"),
    INSTRUCTION(vaddcuw, 0x10000180, VX, General, 0,
                "Vector Add Carryout Unsigned Word"),
    INSTRUCTION(vaddfp, 0x1000000A, VX, General, VX_VD_VA_VB,
                "Vector Add Floating Point"),
    INSTRUCTION(vaddsbs, 0x10000300, VX, General, 0,
                "Vector Add Signed Byte Saturate"),
    INSTRUCTION(vaddshs, 0x10000340, VX, General, 0,
                "Vector Add Signed Half Word Saturate"),
    INSTRUCTION(vaddsws, 0x10000380, VX, General, 0,
                "Vector Add Signed Word Saturate"),
    INSTRUCTION(vaddubm, 0x10000000, VX, General, 0,
                "Vector Add Unsigned Byte Modulo"),
    INSTRUCTION(vaddubs, 0x10000200, VX, General, 0,
                "Vector Add Unsigned Byte Saturate"),
    INSTRUCTION(vadduhm, 0x10000040, VX, General, 0,
                "Vector Add Unsigned Half Word Modulo"),
    INSTRUCTION(vadduhs, 0x10000240, VX, General, 0,
                "Vector Add Unsigned Half Word Saturate"),
    INSTRUCTION(vadduwm, 0x10000080, VX, General, 0,
                "Vector Add Unsigned Word Modulo"),
    INSTRUCTION(vadduws, 0x10000280, VX, General, 0,
                "Vector Add Unsigned Word Saturate"),
    INSTRUCTION(vand, 0x10000404, VX, General, VX_VD_VA_VB,
                "Vector Logical AND"),
    INSTRUCTION(vandc, 0x10000444, VX, General, VX_VD_VA_VB,
                "Vector Logical AND with Complement"),
    INSTRUCTION(vavgsb, 0x10000502, VX, General, 0,
                "Vector Average Signed Byte"),
    INSTRUCTION(vavgsh, 0x10000542, VX, General, 0,
                "Vector Average Signed Half Word"),
    INSTRUCTION(vavgsw, 0x10000582, VX, General, 0,
                "Vector Average Signed Word"),
    INSTRUCTION(vavgub, 0x10000402, VX, General, 0,
                "Vector Average Unsigned Byte"),
    INSTRUCTION(vavguh, 0x10000442, VX, General, 0,
                "Vector Average Unsigned Half Word"),
    INSTRUCTION(vavguw, 0x10000482, VX, General, 0,
                "Vector Average Unsigned Word"),
    INSTRUCTION(vcfsx, 0x1000034A, VX, General, 0,
                "Vector Convert from Signed Fixed-Point Word"),
    INSTRUCTION(vcfux, 0x1000030A, VX, General, 0,
                "Vector Convert from Unsigned Fixed-Point Word"),
    INSTRUCTION(vctsxs, 0x100003CA, VX, General, 0,
                "Vector Convert to Signed Fixed-Point Word Saturate"),
    INSTRUCTION(vctuxs, 0x1000038A, VX, General, 0,
                "Vector Convert to Unsigned Fixed-Point Word Saturate"),
    INSTRUCTION(vexptefp, 0x1000018A, VX, General, 0,
                "Vector 2 Raised to the Exponent Estimate Floating Point"),
    INSTRUCTION(vlogefp, 0x100001CA, VX, General, 0,
                "Vector Log2 Estimate Floating Point"),
    INSTRUCTION(vmaxfp, 0x1000040A, VX, General, 0,
                "Vector Maximum Floating Point"),
    INSTRUCTION(vmaxsb, 0x10000102, VX, General, 0,
                "Vector Maximum Signed Byte"),
    INSTRUCTION(vmaxsh, 0x10000142, VX, General, 0,
                "Vector Maximum Signed Half Word"),
    INSTRUCTION(vmaxsw, 0x10000182, VX, General, 0,
                "Vector Maximum Signed Word"),
    INSTRUCTION(vmaxub, 0x10000002, VX, General, 0,
                "Vector Maximum Unsigned Byte"),
    INSTRUCTION(vmaxuh, 0x10000042, VX, General, 0,
                "Vector Maximum Unsigned Half Word"),
    INSTRUCTION(vmaxuw, 0x10000082, VX, General, 0,
                "Vector Maximum Unsigned Word"),
    INSTRUCTION(vminfp, 0x1000044A, VX, General, 0,
                "Vector Minimum Floating Point"),
    INSTRUCTION(vminsb, 0x10000302, VX, General, 0,
                "Vector Minimum Signed Byte"),
    INSTRUCTION(vminsh, 0x10000342, VX, General, 0,
                "Vector Minimum Signed Half Word"),
    INSTRUCTION(vminsw, 0x10000382, VX, General, 0,
                "Vector Minimum Signed Word"),
    INSTRUCTION(vminub, 0x10000202, VX, General, 0,
                "Vector Minimum Unsigned Byte"),
    INSTRUCTION(vminuh, 0x10000242, VX, General, 0,
                "Vector Minimum Unsigned Half Word"),
    INSTRUCTION(vminuw, 0x10000282, VX, General, 0,
                "Vector Minimum Unsigned Word"),
    INSTRUCTION(vmrghb, 0x1000000C, VX, General, 0, "Vector Merge High Byte"),
    INSTRUCTION(vmrghh, 0x1000004C, VX, General, 0,
                "Vector Merge High Half Word"),
    INSTRUCTION(vmrghw, 0x1000008C, VX, General, 0, "Vector Merge High Word"),
    INSTRUCTION(vmrglb, 0x1000010C, VX, General, 0, "Vector Merge Low Byte"),
    INSTRUCTION(vmrglh, 0x1000014C, VX, General, 0,
                "Vector Merge Low Half Word"),
    INSTRUCTION(vmrglw, 0x1000018C, VX, General, 0, "Vector Merge Low Word"),
    INSTRUCTION(vmulesb, 0x10000308, VX, General, 0,
                "Vector Multiply Even Signed Byte"),
    INSTRUCTION(vmulesh, 0x10000348, VX, General, 0,
                "Vector Multiply Even Signed Half Word"),
    INSTRUCTION(vmuleub, 0x10000208, VX, General, 0,
                "Vector Multiply Even Unsigned Byte"),
    INSTRUCTION(vmuleuh, 0x10000248, VX, General, 0,
                "Vector Multiply Even Unsigned Half Word"),
    INSTRUCTION(vmulosb, 0x10000108, VX, General, 0,
                "Vector Multiply Odd Signed Byte"),
    INSTRUCTION(vmulosh, 0x10000148, VX, General, 0,
                "Vector Multiply Odd Signed Half Word"),
    INSTRUCTION(vmuloub, 0x10000008, VX, General, 0,
                "Vector Multiply Odd Unsigned Byte"),
    INSTRUCTION(vmulouh, 0x10000048, VX, General, 0,
                "Vector Multiply Odd Unsigned Half Word"),
    INSTRUCTION(vnor, 0x10000504, VX, General, VX_VD_VA_VB,
                "Vector Logical NOR"),
    INSTRUCTION(vor, 0x10000484, VX, General, VX_VD_VA_VB, "Vector Logical OR"),
    INSTRUCTION(vpkpx, 0x1000030E, VX, General, 0, "Vector Pack Pixel"),
    INSTRUCTION(vpkshss, 0x1000018E, VX, General, 0,
                "Vector Pack Signed Half Word Signed Saturate"),
    INSTRUCTION(vpkshus, 0x1000010E, VX, General, 0,
                "Vector Pack Signed Half Word Unsigned Saturate"),
    INSTRUCTION(vpkswss, 0x100001CE, VX, General, 0,
                "Vector Pack Signed Word Signed Saturate"),
    INSTRUCTION(vpkswus, 0x1000014E, VX, General, 0,
                "Vector Pack Signed Word Unsigned Saturate"),
    INSTRUCTION(vpkuhum, 0x1000000E, VX, General, 0,
                "Vector Pack Unsigned Half Word Unsigned Modulo"),
    INSTRUCTION(vpkuhus, 0x1000008E, VX, General, 0,
                "Vector Pack Unsigned Half Word Unsigned Saturate"),
    INSTRUCTION(vpkuwum, 0x1000004E, VX, General, 0,
                "Vector Pack Unsigned Word Unsigned Modulo"),
    INSTRUCTION(vpkuwus, 0x100000CE, VX, General, 0,
                "Vector Pack Unsigned Word Unsigned Saturate"),
    INSTRUCTION(vrefp, 0x1000010A, VX, General, 0,
                "Vector Reciprocal Estimate Floating Point"),
    INSTRUCTION(vrfim, 0x100002CA, VX, General, 0,
                "Vector Round to Floating-Point Integer toward -Infinity"),
    INSTRUCTION(vrfin, 0x1000020A, VX, General, 0,
                "Vector Round to Floating-Point Integer Nearest"),
    INSTRUCTION(vrfip, 0x1000028A, VX, General, 0,
                "Vector Round to Floating-Point Integer toward +Infinity"),
    INSTRUCTION(vrfiz, 0x1000024A, VX, General, 0,
                "Vector Round to Floating-Point Integer toward Zero"),
    INSTRUCTION(vrlb, 0x10000004, VX, General, 0,
                "Vector Rotate Left Integer Byte"),
    INSTRUCTION(vrlh, 0x10000044, VX, General, 0,
                "Vector Rotate Left Integer Half Word"),
    INSTRUCTION(vrlw, 0x10000084, VX, General, 0,
                "Vector Rotate Left Integer Word"),
    INSTRUCTION(vrsqrtefp, 0x1000014A, VX, General, 0,
                "Vector Reciprocal Square Root Estimate Floating Point"),
    INSTRUCTION(vsl, 0x100001C4, VX, General, 0, "Vector Shift Left"),
    INSTRUCTION(vslb, 0x10000104, VX, General, VX_VD_VA_VB,
                "Vector Shift Left Integer Byte"),
    INSTRUCTION(vslh, 0x10000144, VX, General, 0,
                "Vector Shift Left Integer Half Word"),
    INSTRUCTION(vslo, 0x1000040C, VX, General, 0, "Vector Shift Left by Octet"),
    INSTRUCTION(vslw, 0x10000184, VX, General, 0,
                "Vector Shift Left Integer Word"),
    INSTRUCTION(vspltb, 0x1000020C, VX, General, vspltb, "Vector Splat Byte"),
    INSTRUCTION(vsplth, 0x1000024C, VX, General, vsplth,
                "Vector Splat Half Word"),
    INSTRUCTION(vspltisb, 0x1000030C, VX, General, vspltisb,
                "Vector Splat Immediate Signed Byte"),
    INSTRUCTION(vspltish, 0x1000034C, VX, General, vspltish,
                "Vector Splat Immediate Signed Half Word"),
    INSTRUCTION(vspltisw, 0x1000038C, VX, General, vspltisw,
                "Vector Splat Immediate Signed Word"),
    INSTRUCTION(vspltw, 0x1000028C, VX, General, vspltw, "Vector Splat Word"),
    INSTRUCTION(vsr, 0x100002C4, VX, General, VX_VD_VA_VB,
                "Vector Shift Right"),
    INSTRUCTION(vsrab, 0x10000304, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Algebraic Byte"),
    INSTRUCTION(vsrah, 0x10000344, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Algebraic Half Word"),
    INSTRUCTION(vsraw, 0x10000384, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Algebraic Word"),
    INSTRUCTION(vsrb, 0x10000204, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Byte"),
    INSTRUCTION(vsrh, 0x10000244, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Half Word"),
    INSTRUCTION(vsro, 0x1000044C, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Octet"),
    INSTRUCTION(vsrw, 0x10000284, VX, General, VX_VD_VA_VB,
                "Vector Shift Right Word"),
    INSTRUCTION(vsubcuw, 0x10000580, VX, General, 0,
                "Vector Subtract Carryout Unsigned Word"),
    INSTRUCTION(vsubfp, 0x1000004A, VX, General, 0,
                "Vector Subtract Floating Point"),
    INSTRUCTION(vsubsbs, 0x10000700, VX, General, 0,
                "Vector Subtract Signed Byte Saturate"),
    INSTRUCTION(vsubshs, 0x10000740, VX, General, 0,
                "Vector Subtract Signed Half Word Saturate"),
    INSTRUCTION(vsubsws, 0x10000780, VX, General, 0,
                "Vector Subtract Signed Word Saturate"),
    INSTRUCTION(vsububm, 0x10000400, VX, General, 0,
                "Vector Subtract Unsigned Byte Modulo"),
    INSTRUCTION(vsububs, 0x10000600, VX, General, 0,
                "Vector Subtract Unsigned Byte Saturate"),
    INSTRUCTION(vsubuhm, 0x10000440, VX, General, 0,
                "Vector Subtract Unsigned Half Word Modulo"),
    INSTRUCTION(vsubuhs, 0x10000640, VX, General, 0,
                "Vector Subtract Unsigned Half Word Saturate"),
    INSTRUCTION(vsubuwm, 0x10000480, VX, General, 0,
                "Vector Subtract Unsigned Word Modulo"),
    INSTRUCTION(vsubuws, 0x10000680, VX, General, 0,
                "Vector Subtract Unsigned Word Saturate"),
    INSTRUCTION(vsumsws, 0x10000788, VX, General, 0,
                "Vector Sum Across Signed Word Saturate"),
    INSTRUCTION(vsum2sws, 0x10000688, VX, General, 0,
                "Vector Sum Across Partial (1/2) Signed Word Saturate"),
    INSTRUCTION(vsum4sbs, 0x10000708, VX, General, 0,
                "Vector Sum Across Partial (1/4) Signed Byte Saturate"),
    INSTRUCTION(vsum4shs, 0x10000648, VX, General, 0,
                "Vector Sum Across Partial (1/4) Signed Half Word Saturate"),
    INSTRUCTION(vsum4ubs, 0x10000608, VX, General, 0,
                "Vector Sum Across Partial (1/4) Unsigned Byte Saturate"),
    INSTRUCTION(vupkhpx, 0x1000034E, VX, General, 0,
                "Vector Unpack High Pixel"),
    INSTRUCTION(vupkhsb, 0x1000020E, VX, General, 0,
                "Vector Unpack High Signed Byte"),
    INSTRUCTION(vupkhsh, 0x1000024E, VX, General, 0,
                "Vector Unpack High Signed Half Word"),
    INSTRUCTION(vupklpx, 0x100003CE, VX, General, 0, "Vector Unpack Low Pixel"),
    INSTRUCTION(vupklsb, 0x1000028E, VX, General, 0,
                "Vector Unpack Low Signed Byte"),
    INSTRUCTION(vupklsh, 0x100002CE, VX, General, 0,
                "Vector Unpack Low Signed Half Word"),
    INSTRUCTION(vxor, 0x100004C4, VX, General, VX_VD_VA_VB,
                "Vector Logical XOR"),
};
static InstrType** instr_table_4 = instr_table_prep(
    instr_table_4_unprep, xe::countof(instr_table_4_unprep), 0, 11);

// Opcode = 19, index = bits 10-1 (10)
static InstrType instr_table_19_unprep[] = {
    INSTRUCTION(mcrf, 0x4C000000, XL, General, 0, NULL),
    INSTRUCTION(bclrx, 0x4C000020, XL, BranchCond, bclrx,
                "Branch Conditional to Link Register"),
    INSTRUCTION(crnor, 0x4C000042, XL, General, 0, NULL),
    INSTRUCTION(crandc, 0x4C000102, XL, General, 0, NULL),
    INSTRUCTION(isync, 0x4C00012C, XL, General, 0, NULL),
    INSTRUCTION(crxor, 0x4C000182, XL, General, 0, NULL),
    INSTRUCTION(crnand, 0x4C0001C2, XL, General, 0, NULL),
    INSTRUCTION(crand, 0x4C000202, XL, General, 0, NULL),
    INSTRUCTION(creqv, 0x4C000242, XL, General, 0, NULL),
    INSTRUCTION(crorc, 0x4C000342, XL, General, 0, NULL),
    INSTRUCTION(cror, 0x4C000382, XL, General, 0, NULL),
    INSTRUCTION(bcctrx, 0x4C000420, XL, BranchCond, bcctrx,
                "Branch Conditional to Count Register"),
};
static InstrType** instr_table_19 = instr_table_prep(
    instr_table_19_unprep, xe::countof(instr_table_19_unprep), 1, 10);

// Opcode = 30, index = bits 4-1 (4)
static InstrType instr_table_30_unprep[] = {
    // Decoding these instrunctions in this table is difficult because the
    // index bits are kind of random. This is special cased by an uber
    // instruction handler.
    INSTRUCTION(rld, 0x78000000, MD, General, rld, NULL),
    // INSTRUCTION(rldiclx,        0x78000000, MD , General        , 0),
    // INSTRUCTION(rldicrx,        0x78000004, MD , General        , 0),
    // INSTRUCTION(rldicx,         0x78000008, MD , General        , 0),
    // INSTRUCTION(rldimix,        0x7800000C, MD , General        , 0),
    // INSTRUCTION(rldclx,         0x78000010, MDS, General        , 0),
    // INSTRUCTION(rldcrx,         0x78000012, MDS, General        , 0),
};
static InstrType** instr_table_30 = instr_table_prep(
    instr_table_30_unprep, xe::countof(instr_table_30_unprep), 0, 0);

// Opcode = 31, index = bits 10-1 (10)
static InstrType instr_table_31_unprep[] = {
    INSTRUCTION(cmp, 0x7C000000, X, General, cmp, "Compare"),
    INSTRUCTION(tw, 0x7C000008, X, General, X_RA_RB, "Trap Word"),
    INSTRUCTION(lvsl, 0x7C00000C, X, General, X_VX_RA0_RB,
                "Load Vector for Shift Left"),
    INSTRUCTION(lvebx, 0x7C00000E, X, General, 0,
                "Load Vector Element Byte Indexed"),
    INSTRUCTION(subfcx, 0x7C000010, XO, General, XO_RT_RA_RB,
                "Subtract From Carrying"),
    INSTRUCTION(mulhdux, 0x7C000012, XO, General, XO_RT_RA_RB,
                "Multiply High Doubleword Unsigned"),
    INSTRUCTION(addcx, 0x7C000014, XO, General, XO_RT_RA_RB, "Add Carrying"),
    INSTRUCTION(mulhwux, 0x7C000016, XO, General, XO_RT_RA_RB,
                "Multiply High Word Unsigned"),
    INSTRUCTION(mfcr, 0x7C000026, X, General, mfcr,
                "Move From Condition Register"),
    INSTRUCTION(lwarx, 0x7C000028, X, General, X_RT_RA0_RB,
                "Load Word And Reserve Indexed"),
    INSTRUCTION(ldx, 0x7C00002A, X, General, X_RT_RA0_RB,
                "Load Doubleword Indexed"),
    INSTRUCTION(lwzx, 0x7C00002E, X, General, X_RT_RA0_RB,
                "Load Word and Zero Indexed"),
    INSTRUCTION(slwx, 0x7C000030, X, General, X_RA_RT_RB, "Shift Left Word"),
    INSTRUCTION(cntlzwx, 0x7C000034, X, General, X_RA_RT,
                "Count Leading Zeros Word"),
    INSTRUCTION(sldx, 0x7C000036, X, General, X_RA_RT_RB,
                "Shift Left Doubleword"),
    INSTRUCTION(andx, 0x7C000038, X, General, X_RA_RT_RB, "AND"),
    INSTRUCTION(cmpl, 0x7C000040, X, General, cmp, "Compare Logical"),
    INSTRUCTION(lvsr, 0x7C00004C, X, General, X_VX_RA0_RB,
                "Load Vector for Shift Right"),
    INSTRUCTION(lvehx, 0x7C00004E, X, General, 0,
                "Load Vector Element Half Word Indexed"),
    INSTRUCTION(subfx, 0x7C000050, XO, General, XO_RT_RA_RB, "Subtract From"),
    INSTRUCTION(ldux, 0x7C00006A, X, General, X_RT_RA_RB,
                "Load Doubleword with Update Indexed"),
    INSTRUCTION(dcbst, 0x7C00006C, X, General, 0, NULL),
    INSTRUCTION(lwzux, 0x7C00006E, X, General, X_RT_RA_RB,
                "Load Word and Zero with Update Indexed"),
    INSTRUCTION(cntlzdx, 0x7C000074, X, General, X_RA_RT,
                "Count Leading Zeros Doubleword"),
    INSTRUCTION(andcx, 0x7C000078, X, General, X_RA_RT_RB,
                "AND with Complement"),
    INSTRUCTION(td, 0x7C000088, X, General, X_RA_RB, "Trap Doubleword"),
    INSTRUCTION(lvewx, 0x7C00008E, X, General, 0,
                "Load Vector Element Word Indexed"),
    INSTRUCTION(mulhdx, 0x7C000092, XO, General, XO_RT_RA_RB,
                "Multiply High Doubleword"),
    INSTRUCTION(mulhwx, 0x7C000096, XO, General, XO_RT_RA_RB,
                "Multiply High Word"),
    INSTRUCTION(mfmsr, 0x7C0000A6, X, General, mfmsr,
                "Move From Machine State Register"),
    INSTRUCTION(ldarx, 0x7C0000A8, X, General, X_RT_RA0_RB,
                "Load Doubleword And Reserve Indexed"),
    INSTRUCTION(dcbf, 0x7C0000AC, X, General, dcbf, "Data Cache Block Flush"),
    INSTRUCTION(lbzx, 0x7C0000AE, X, General, X_RT_RA0_RB,
                "Load Byte and Zero Indexed"),
    INSTRUCTION(lvx, 0x7C0000CE, X, General, X_VX_RA0_RB,
                "Load Vector Indexed"),
    INSTRUCTION(negx, 0x7C0000D0, XO, General, XO_RT_RA, "Negate"),
    INSTRUCTION(lbzux, 0x7C0000EE, X, General, X_RT_RA_RB,
                "Load Byte and Zero with Update Indexed"),
    INSTRUCTION(norx, 0x7C0000F8, X, General, X_RA_RT_RB, "NOR"),
    INSTRUCTION(stvebx, 0x7C00010E, X, General, 0,
                "Store Vector Element Byte Indexed"),
    INSTRUCTION(subfex, 0x7C000110, XO, General, XO_RT_RA_RB,
                "Subtract From Extended"),
    INSTRUCTION(addex, 0x7C000114, XO, General, XO_RT_RA_RB, "Add Extended"),
    INSTRUCTION(mtcrf, 0x7C000120, XFX, General, 0, NULL),
    INSTRUCTION(mtmsr, 0x7C000124, X, General, mtmsr,
                "Move To Machine State Register"),
    INSTRUCTION(stdx, 0x7C00012A, X, General, X_RT_RA0_RB,
                "Store Doubleword Indexed"),
    INSTRUCTION(stwcx, 0x7C00012D, X, General, X_RT_RA0_RB,
                "Store Word Conditional Indexed"),
    INSTRUCTION(stwx, 0x7C00012E, X, General, X_RT_RA0_RB,
                "Store Word Indexed"),
    INSTRUCTION(stvehx, 0x7C00014E, X, General, 0,
                "Store Vector Element Half Word Indexed"),
    INSTRUCTION(mtmsrd, 0x7C000164, X, General, mtmsr,
                "Move To Machine State Register Doubleword"),
    INSTRUCTION(stdux, 0x7C00016A, X, General, X_RT_RA_RB,
                "Store Doubleword with Update Indexed"),
    INSTRUCTION(stwux, 0x7C00016E, X, General, X_RT_RA_RB,
                "Store Word with Update Indexed"),
    INSTRUCTION(stvewx, 0x7C00018E, X, General, 0,
                "Store Vector Element Word Indexed"),
    INSTRUCTION(subfzex, 0x7C000190, XO, General, XO_RT_RA,
                "Subtract From Zero Extended"),
    INSTRUCTION(addzex, 0x7C000194, XO, General, XO_RT_RA,
                "Add to Zero Extended"),
    INSTRUCTION(stdcx, 0x7C0001AD, X, General, X_RT_RA0_RB,
                "Store Doubleword Conditional Indexed"),
    INSTRUCTION(stbx, 0x7C0001AE, X, General, X_RT_RA0_RB,
                "Store Byte Indexed"),
    INSTRUCTION(stvx, 0x7C0001CE, X, General, 0, "Store Vector Indexed"),
    INSTRUCTION(subfmex, 0x7C0001D0, XO, General, XO_RT_RA,
                "Subtract From Minus One Extended"),
    INSTRUCTION(mulldx, 0x7C0001D2, XO, General, XO_RT_RA_RB,
                "Multiply Low Doubleword"),
    INSTRUCTION(addmex, 0x7C0001D4, XO, General, XO_RT_RA,
                "Add to Minus One Extended"),
    INSTRUCTION(mullwx, 0x7C0001D6, XO, General, XO_RT_RA_RB,
                "Multiply Low Word"),
    INSTRUCTION(dcbtst, 0x7C0001EC, X, General, 0,
                "Data Cache Block Touch for Store"),
    INSTRUCTION(stbux, 0x7C0001EE, X, General, X_RT_RA_RB,
                "Store Byte with Update Indexed"),
    INSTRUCTION(addx, 0x7C000214, XO, General, XO_RT_RA_RB, "Add"),
    INSTRUCTION(dcbt, 0x7C00022C, X, General, 0, "Data Cache Block Touch"),
    INSTRUCTION(lhzx, 0x7C00022E, X, General, X_RT_RA0_RB,
                "Load Halfword and Zero Indexed"),
    INSTRUCTION(eqvx, 0x7C000238, X, General, X_RA_RT_RB, "Equivalent"),
    INSTRUCTION(eciwx, 0x7C00026C, X, General, 0, NULL),
    INSTRUCTION(lhzux, 0x7C00026E, X, General, X_RT_RA_RB,
                "Load Halfword and Zero with Update Indexed"),
    INSTRUCTION(xorx, 0x7C000278, X, General, X_RA_RT_RB, "XOR"),
    INSTRUCTION(mfspr, 0x7C0002A6, XFX, General, mfspr,
                "Move From Special Purpose Register"),
    INSTRUCTION(lwax, 0x7C0002AA, X, General, X_RT_RA0_RB,
                "Load Word Algebraic Indexed"),
    INSTRUCTION(lhax, 0x7C0002AE, X, General, X_RT_RA0_RB,
                "Load Halfword Algebraic Indexed"),
    INSTRUCTION(lvxl, 0x7C0002CE, X, General, X_VX_RA0_RB,
                "Load Vector Indexed LRU"),
    INSTRUCTION(mftb, 0x7C0002E6, XFX, General, mftb, "Move From Time Base"),
    INSTRUCTION(lwaux, 0x7C0002EA, X, General, X_RT_RA_RB,
                "Load Word Algebraic with Update Indexed"),
    INSTRUCTION(lhaux, 0x7C0002EE, X, General, 0, NULL),
    INSTRUCTION(sthx, 0x7C00032E, X, General, X_RT_RA0_RB,
                "Store Halfword Indexed"),
    INSTRUCTION(orcx, 0x7C000338, X, General, X_RA_RT_RB, "OR with Complement"),
    INSTRUCTION(ecowx, 0x7C00036C, X, General, 0, NULL),
    INSTRUCTION(sthux, 0x7C00036E, X, General, X_RT_RA_RB,
                "Store Halfword with Update Indexed"),
    INSTRUCTION(orx, 0x7C000378, X, General, X_RA_RT_RB, "OR"),
    INSTRUCTION(divdux, 0x7C000392, XO, General, XO_RT_RA_RB,
                "Divide Doubleword Unsigned"),
    INSTRUCTION(divwux, 0x7C000396, XO, General, XO_RT_RA_RB,
                "Divide Word Unsigned"),
    INSTRUCTION(mtspr, 0x7C0003A6, XFX, General, mtspr,
                "Move To Special Purpose Register"),
    INSTRUCTION(nandx, 0x7C0003B8, X, General, X_RA_RT_RB, "NAND"),
    INSTRUCTION(stvxl, 0x7C0003CE, X, General, 0, "Store Vector Indexed LRU"),
    INSTRUCTION(divdx, 0x7C0003D2, XO, General, XO_RT_RA_RB,
                "Divide Doubleword"),
    INSTRUCTION(divwx, 0x7C0003D6, XO, General, XO_RT_RA_RB, "Divide Word"),
    INSTRUCTION(lvlx, 0x7C00040E, X, General, 0, "Load Vector Indexed"),
    INSTRUCTION(ldbrx, 0x7C000428, X, General, X_RT_RA0_RB,
                "Load Doubleword Byte-Reverse Indexed"),
    INSTRUCTION(lswx, 0x7C00042A, X, General, 0, NULL),
    INSTRUCTION(lwbrx, 0x7C00042C, X, General, X_RT_RA0_RB,
                "Load Word Byte-Reverse Indexed"),
    INSTRUCTION(lfsx, 0x7C00042E, X, General, X_FRT_RA0_RB,
                "Load Floating-Point Single Indexed"),
    INSTRUCTION(srwx, 0x7C000430, X, General, X_RA_RT_RB, "Shift Right Word"),
    INSTRUCTION(srdx, 0x7C000436, X, General, X_RA_RT_RB,
                "Shift Right Doubleword"),
    INSTRUCTION(lfsux, 0x7C00046E, X, General, X_FRT_RA_RB,
                "Load Floating-Point Single with Update Indexed"),
    INSTRUCTION(lswi, 0x7C0004AA, X, General, 0, NULL),
    INSTRUCTION(sync, 0x7C0004AC, X, General, sync, "Synchronize"),
    INSTRUCTION(lfdx, 0x7C0004AE, X, General, X_FRT_RA0_RB,
                "Load Floating-Point Double Indexed"),
    INSTRUCTION(lfdux, 0x7C0004EE, X, General, X_FRT_RA_RB,
                "Load Floating-Point Double with Update Indexed"),
    INSTRUCTION(stdbrx, 0x7C000528, X, General, X_RT_RA0_RB,
                "Store Doubleword Byte-Reverse Indexed"),
    INSTRUCTION(stswx, 0x7C00052A, X, General, 0, NULL),
    INSTRUCTION(stwbrx, 0x7C00052C, X, General, X_RT_RA0_RB,
                "Store Word Byte-Reverse Indexed"),
    INSTRUCTION(stfsx, 0x7C00052E, X, General, X_FRT_RA0_RB,
                "Store Floating-Point Single Indexed"),
    INSTRUCTION(stfsux, 0x7C00056E, X, General, X_FRT_RA_RB,
                "Store Floating-Point Single with Update Indexed"),
    INSTRUCTION(stswi, 0x7C0005AA, X, General, 0, NULL),
    INSTRUCTION(stfdx, 0x7C0005AE, X, General, X_FRT_RA0_RB,
                "Store Floating-Point Double Indexed"),
    INSTRUCTION(stfdux, 0x7C0005EE, X, General, X_FRT_RA_RB,
                "Store Floating-Point Double with Update Indexed"),
    INSTRUCTION(lhbrx, 0x7C00062C, X, General, X_RT_RA0_RB,
                "Load Halfword Byte-Reverse Indexed"),
    INSTRUCTION(srawx, 0x7C000630, X, General, X_RA_RT_RB,
                "Shift Right Algebraic Word"),
    INSTRUCTION(sradx, 0x7C000634, X, General, X_RA_RT_RB,
                "Shift Right Algebraic Doubleword"),
    INSTRUCTION(srawix, 0x7C000670, X, General, srawix,
                "Shift Right Algebraic Word Immediate"),
    INSTRUCTION(sradix, 0x7C000674, XS, General, sradix,
                "Shift Right Algebraic Doubleword Immediate"),
    INSTRUCTION(sradix, 0x7C000674, XS, General, sradix,
                "Shift Right Algebraic Doubleword Immediate"),
    INSTRUCTION(sradix, 0x7C000676, XS, General, sradix,
                "Shift Right Algebraic Doubleword Immediate"),  // HACK
    INSTRUCTION(eieio, 0x7C0006AC, X, General, _,
                "Enforce In-Order Execution of I/O Instruction"),
    INSTRUCTION(sthbrx, 0x7C00072C, X, General, X_RT_RA0_RB,
                "Store Halfword Byte-Reverse Indexed"),
    INSTRUCTION(extshx, 0x7C000734, X, General, X_RA_RT,
                "Extend Sign Halfword"),
    INSTRUCTION(extsbx, 0x7C000774, X, General, X_RA_RT, "Extend Sign Byte"),
    INSTRUCTION(icbi, 0x7C0007AC, X, General, 0, NULL),
    INSTRUCTION(stfiwx, 0x7C0007AE, X, General, X_FRT_RA0_RB,
                "Store Floating-Point as Integer Word Indexed"),
    INSTRUCTION(extswx, 0x7C0007B4, X, General, X_RA_RT, "Extend Sign Word"),
    INSTRUCTION(dcbz, 0x7C0007EC, X, General, dcbz,
                "Data Cache Block set to Zero"),  // 0x7C2007EC = DCBZ128
    INSTRUCTION(dst, 0x7C0002AC, XDSS, General, 0, NULL),
    INSTRUCTION(dstst, 0x7C0002EC, XDSS, General, 0, NULL),
    INSTRUCTION(dss, 0x7C00066C, XDSS, General, 0, NULL),
    INSTRUCTION(lvebx, 0x7C00000E, X, General, 0,
                "Load Vector Element Byte Indexed"),
    INSTRUCTION(lvehx, 0x7C00004E, X, General, 0,
                "Load Vector Element Half Word Indexed"),
    INSTRUCTION(lvewx, 0x7C00008E, X, General, 0,
                "Load Vector Element Word Indexed"),
    INSTRUCTION(lvsl, 0x7C00000C, X, General, 0, "Load Vector for Shift Left"),
    INSTRUCTION(lvsr, 0x7C00004C, X, General, 0, "Load Vector for Shift Right"),
    INSTRUCTION(lvx, 0x7C0000CE, X, General, 0, "Load Vector Indexed"),
    INSTRUCTION(lvxl, 0x7C0002CE, X, General, 0, "Load Vector Indexed LRU"),
    INSTRUCTION(stvebx, 0x7C00010E, X, General, 0,
                "Store Vector Element Byte Indexed"),
    INSTRUCTION(stvehx, 0x7C00014E, X, General, 0,
                "Store Vector Element Half Word Indexed"),
    INSTRUCTION(stvewx, 0x7C00018E, X, General, 0,
                "Store Vector Element Word Indexed"),
    INSTRUCTION(stvx, 0x7C0001CE, X, General, X_VX_RA0_RB,
                "Store Vector Indexed"),
    INSTRUCTION(stvxl, 0x7C0003CE, X, General, X_VX_RA0_RB,
                "Store Vector Indexed LRU"),
    INSTRUCTION(lvlx, 0x7C00040E, X, General, X_VX_RA0_RB,
                "Load Vector Left Indexed"),
    INSTRUCTION(lvlxl, 0x7C00060E, X, General, X_VX_RA0_RB,
                "Load Vector Left Indexed LRU"),
    INSTRUCTION(lvrx, 0x7C00044E, X, General, X_VX_RA0_RB,
                "Load Vector Right Indexed"),
    INSTRUCTION(lvrxl, 0x7C00064E, X, General, X_VX_RA0_RB,
                "Load Vector Right Indexed LRU"),
    INSTRUCTION(stvlx, 0x7C00050E, X, General, X_VX_RA0_RB,
                "Store Vector Left Indexed"),
    INSTRUCTION(stvlxl, 0x7C00070E, X, General, X_VX_RA0_RB,
                "Store Vector Left Indexed LRU"),
    INSTRUCTION(stvrx, 0x7C00054E, X, General, X_VX_RA0_RB,
                "Store Vector Right Indexed"),
    INSTRUCTION(stvrxl, 0x7C00074E, X, General, X_VX_RA0_RB,
                "Store Vector Right Indexed LRU"),
};
static InstrType** instr_table_31 = instr_table_prep(
    instr_table_31_unprep, xe::countof(instr_table_31_unprep), 1, 10);

// Opcode = 58, index = bits 1-0 (2)
static InstrType instr_table_58_unprep[] = {
    INSTRUCTION(ld, 0xE8000000, DS, General, DS_RT_RA0_I, "Load Doubleword"),
    INSTRUCTION(ldu, 0xE8000001, DS, General, DS_RT_RA_I,
                "Load Doubleword with Update"),
    INSTRUCTION(lwa, 0xE8000002, DS, General, DS_RT_RA0_I,
                "Load Word Algebraic"),
};
static InstrType** instr_table_58 = instr_table_prep(
    instr_table_58_unprep, xe::countof(instr_table_58_unprep), 0, 1);

// Opcode = 59, index = bits 5-1 (5)
static InstrType instr_table_59_unprep[] = {
    INSTRUCTION(fdivsx, 0xEC000024, A, General, A_FRT_FRA_FRB,
                "Floating Divide [Single]"),
    INSTRUCTION(fsubsx, 0xEC000028, A, General, A_FRT_FRA_FRB,
                "Floating Subtract [Single]"),
    INSTRUCTION(faddsx, 0xEC00002A, A, General, A_FRT_FRA_FRB,
                "Floating Add [Single]"),
    INSTRUCTION(fsqrtsx, 0xEC00002C, A, General, A_FRT_FRB,
                "Floating Square Root [Single]"),
    INSTRUCTION(fresx, 0xEC000030, A, General, A_FRT_FRB,
                "Floating Reciprocal Estimate [Single]"),
    INSTRUCTION(fmulsx, 0xEC000032, A, General, A_FRT_FRA_FRB,
                "Floating Multiply [Single]"),
    INSTRUCTION(fmsubsx, 0xEC000038, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Multiply-Subtract [Single]"),
    INSTRUCTION(fmaddsx, 0xEC00003A, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Multiply-Add [Single]"),
    INSTRUCTION(fnmsubsx, 0xEC00003C, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Negative Multiply-Subtract [Single]"),
    INSTRUCTION(fnmaddsx, 0xEC00003E, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Negative Multiply-Add [Single]"),
};
static InstrType** instr_table_59 = instr_table_prep(
    instr_table_59_unprep, xe::countof(instr_table_59_unprep), 1, 5);

// Opcode = 62, index = bits 1-0 (2)
static InstrType instr_table_62_unprep[] = {
    INSTRUCTION(std, 0xF8000000, DS, General, DS_RT_RA0_I, "Store Doubleword"),
    INSTRUCTION(stdu, 0xF8000001, DS, General, DS_RT_RA_I,
                "Store Doubleword with Update"),
};
static InstrType** instr_table_62 = instr_table_prep(
    instr_table_62_unprep, xe::countof(instr_table_62_unprep), 0, 1);

// Opcode = 63, index = bits 10-1 (10)
// NOTE: the A format instructions need some special handling because
//       they only use 6bits to identify their index.
static InstrType instr_table_63_unprep[] = {
    INSTRUCTION(fcmpu, 0xFC000000, X, General, fcmp,
                "Floating Compare Unordered"),
    INSTRUCTION(frspx, 0xFC000018, X, General, X_FRT_FRB,
                "Floating Round to Single-Precision"),
    INSTRUCTION(fctiwx, 0xFC00001C, X, General, X_FRT_FRB,
                "Floating Convert To Integer Word"),
    INSTRUCTION(fctiwzx, 0xFC00001E, X, General, X_FRT_FRB,
                "Floating Convert To Integer Word with round toward Zero"),
    INSTRUCTION(fdivx, 0xFC000024, A, General, A_FRT_FRA_FRB,
                "Floating Divide [Single]"),
    INSTRUCTION(fsubx, 0xFC000028, A, General, A_FRT_FRA_FRB,
                "Floating Subtract [Single]"),
    INSTRUCTION(faddx, 0xFC00002A, A, General, A_FRT_FRA_FRB,
                "Floating Add [Single]"),
    INSTRUCTION(fsqrtx, 0xFC00002C, A, General, A_FRT_FRB,
                "Floating Square Root [Single]"),
    INSTRUCTION(fselx, 0xFC00002E, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Select"),
    INSTRUCTION(fmulx, 0xFC000032, A, General, A_FRT_FRA_FRB,
                "Floating Multiply [Single]"),
    INSTRUCTION(frsqrtex, 0xFC000034, A, General, A_FRT_FRB,
                "Floating Reciprocal Square Root Estimate [Single]"),
    INSTRUCTION(fmsubx, 0xFC000038, A, General, 0,
                "Floating Multiply-Subtract [Single]"),
    INSTRUCTION(fmaddx, 0xFC00003A, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Multiply-Add [Single]"),
    INSTRUCTION(fnmsubx, 0xFC00003C, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Negative Multiply-Subtract [Single]"),
    INSTRUCTION(fnmaddx, 0xFC00003E, A, General, A_FRT_FRA_FRB_FRC,
                "Floating Negative Multiply-Add [Single]"),
    INSTRUCTION(fcmpo, 0xFC000040, X, General, fcmp,
                "Floating Compare Ordered"),
    INSTRUCTION(mtfsb1x, 0xFC00004C, X, General, 0, NULL),
    INSTRUCTION(fnegx, 0xFC000050, X, General, X_FRT_FRB, "Floating Negate"),
    INSTRUCTION(mcrfs, 0xFC000080, X, General, 0, NULL),
    INSTRUCTION(mtfsb0x, 0xFC00008C, X, General, 0, NULL),
    INSTRUCTION(fmrx, 0xFC000090, X, General, X_FRT_FRB,
                "Floating Move Register"),
    INSTRUCTION(mtfsfix, 0xFC00010C, X, General, 0, NULL),
    INSTRUCTION(fnabsx, 0xFC000110, X, General, X_FRT_FRB,
                "Floating Negative Absolute Value"),
    INSTRUCTION(fabsx, 0xFC000210, X, General, X_FRT_FRB,
                "Floating Absolute Value"),
    INSTRUCTION(mffsx, 0xFC00048E, X, General, 0, "Move from FPSCR"),
    INSTRUCTION(mtfsfx, 0xFC00058E, XFL, General, 0, "Move to FPSCR Fields"),
    INSTRUCTION(fctidx, 0xFC00065C, X, General, X_FRT_FRB,
                "Floating Convert To Integer Doubleword"),
    INSTRUCTION(
        fctidzx, 0xFC00065E, X, General, X_FRT_FRB,
        "Floating Convert To Integer Doubleword with round toward Zero"),
    INSTRUCTION(fcfidx, 0xFC00069C, X, General, X_FRT_FRB,
                "Floating Convert From Integer Doubleword"),
};
static InstrType** instr_table_63 = instr_table_prep_63(
    instr_table_63_unprep, xe::countof(instr_table_63_unprep), 1, 10);

// Main table, index = bits 31-26 (6) : (code >> 26)
static InstrType instr_table_unprep[64] = {
    INSTRUCTION(tdi, 0x08000000, D, General, D_RA, "Trap Doubleword Immediate"),
    INSTRUCTION(twi, 0x0C000000, D, General, D_RA, "Trap Word Immediate"),
    INSTRUCTION(mulli, 0x1C000000, D, General, D_RT_RA_I,
                "Multiply Low Immediate"),
    INSTRUCTION(subficx, 0x20000000, D, General, D_RT_RA_I,
                "Subtract From Immediate Carrying"),
    INSTRUCTION(cmpli, 0x28000000, D, General, cmpli,
                "Compare Logical Immediate"),
    INSTRUCTION(cmpi, 0x2C000000, D, General, cmpi, "Compare Immediate"),
    INSTRUCTION(addic, 0x30000000, D, General, D_RT_RA_I,
                "Add Immediate Carrying"),
    INSTRUCTION(addicx, 0x34000000, D, General, D_RT_RA_I,
                "Add Immediate Carrying and Record"),
    INSTRUCTION(addi, 0x38000000, D, General, D_RT_RA0_I, "Add Immediate"),
    INSTRUCTION(addis, 0x3C000000, D, General, D_RT_RA0_I,
                "Add Immediate Shifted"),
    INSTRUCTION(bcx, 0x40000000, B, BranchCond, bcx, "Branch Conditional"),
    INSTRUCTION(sc, 0x44000002, SC, Syscall, 0, NULL),
    INSTRUCTION(bx, 0x48000000, I, BranchAlways, bx, "Branch"),
    INSTRUCTION(rlwimix, 0x50000000, M, General, rlwim,
                "Rotate Left Word Immediate then Mask Insert"),
    INSTRUCTION(rlwinmx, 0x54000000, M, General, rlwim,
                "Rotate Left Word Immediate then AND with Mask"),
    INSTRUCTION(rlwnmx, 0x5C000000, M, General, rlwnmx,
                "Rotate Left Word then AND with Mask"),
    INSTRUCTION(ori, 0x60000000, D, General, D_RA_RT_I, "OR Immediate"),
    INSTRUCTION(oris, 0x64000000, D, General, D_RA_RT_I,
                "OR Immediate Shifted"),
    INSTRUCTION(xori, 0x68000000, D, General, D_RA_RT_I, "XOR Immediate"),
    INSTRUCTION(xoris, 0x6C000000, D, General, D_RA_RT_I,
                "XOR Immediate Shifted"),
    INSTRUCTION(andix, 0x70000000, D, General, D_RA_RT_I, "AND Immediate"),
    INSTRUCTION(andisx, 0x74000000, D, General, D_RA_RT_I,
                "AND Immediate Shifted"),
    INSTRUCTION(lwz, 0x80000000, D, General, D_RT_RA0_I, "Load Word and Zero"),
    INSTRUCTION(lwzu, 0x84000000, D, General, D_RT_RA_I,
                "Load Word and Zero with Udpate"),
    INSTRUCTION(lbz, 0x88000000, D, General, D_RT_RA0_I, "Load Byte and Zero"),
    INSTRUCTION(lbzu, 0x8C000000, D, General, D_RT_RA_I,
                "Load Byte and Zero with Update"),
    INSTRUCTION(stw, 0x90000000, D, General, D_RT_RA0_I, "Store Word"),
    INSTRUCTION(stwu, 0x94000000, D, General, D_RT_RA_I,
                "Store Word with Update"),
    INSTRUCTION(stb, 0x98000000, D, General, D_RT_RA0_I, "Store Byte"),
    INSTRUCTION(stbu, 0x9C000000, D, General, D_RT_RA_I,
                "Store Byte with Update"),
    INSTRUCTION(lhz, 0xA0000000, D, General, D_RT_RA0_I,
                "Load Halfword and Zero"),
    INSTRUCTION(lhzu, 0xA4000000, D, General, D_RT_RA_I,
                "Load Halfword and Zero with Update"),
    INSTRUCTION(lha, 0xA8000000, D, General, D_RT_RA0_I,
                "Load Halfword Algebraic"),
    INSTRUCTION(lhau, 0xAC000000, D, General, D_RT_RA_I, NULL),
    INSTRUCTION(sth, 0xB0000000, D, General, D_RT_RA0_I, "Store Halfword"),
    INSTRUCTION(sthu, 0xB4000000, D, General, D_RT_RA_I,
                "Store Halfword with Update"),
    INSTRUCTION(lmw, 0xB8000000, D, General, 0, NULL),
    INSTRUCTION(stmw, 0xBC000000, D, General, 0, NULL),
    INSTRUCTION(lfs, 0xC0000000, D, General, D_FRT_RA0_I,
                "Load Floating-Point Single"),
    INSTRUCTION(lfsu, 0xC4000000, D, General, D_FRT_RA_I,
                "Load Floating-Point Single with Update"),
    INSTRUCTION(lfd, 0xC8000000, D, General, D_FRT_RA0_I,
                "Load Floating-Point Double"),
    INSTRUCTION(lfdu, 0xCC000000, D, General, D_FRT_RA_I,
                "Load Floating-Point Double with Update"),
    INSTRUCTION(stfs, 0xD0000000, D, General, D_FRT_RA0_I,
                "Store Floating-Point Single"),
    INSTRUCTION(stfsu, 0xD4000000, D, General, D_FRT_RA_I,
                "Store Floating-Point Single with Update"),
    INSTRUCTION(stfd, 0xD8000000, D, General, D_FRT_RA0_I,
                "Store Floating-Point Double"),
    INSTRUCTION(stfdu, 0xDC000000, D, General, D_FRT_RA_I,
                "Store Floating-Point Double with Update"),
};
static InstrType** instr_table = instr_table_prep(
    instr_table_unprep, xe::countof(instr_table_unprep), 26, 31);

// Altivec instructions.
// TODO(benvanik): build a table like the other instructions.
// This table is looked up via linear scan of opcodes.
#define SCAN_INSTRUCTION(name, opcode, format, type, disasm_fn, descr) \
  {                                                                    \
    opcode, kXEPPCInstrMask##format, kXEPPCInstrFormat##format,        \
        kXEPPCInstrType##type, 0, Disasm_##disasm_fn, #name, 0,        \
  }
#define OP(x) ((((uint32_t)(x)) & 0x3f) << 26)
#define VX128(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x3d0))
#define VX128_1(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f3))
#define VX128_2(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x210))
#define VX128_3(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f0))
#define VX128_4(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x730))
#define VX128_5(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x10))
#define VX128_P(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x630))
#define VX128_R(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x390))
static InstrType instr_table_scan[] = {
    SCAN_INSTRUCTION(vcmpbfp, 0x100003C6, VXR, General, 0,
                     "Vector Compare Bounds Floating Point"),
    SCAN_INSTRUCTION(vcmpeqfp, 0x100000C6, VXR, General, 0,
                     "Vector Compare Equal-to Floating Point"),
    SCAN_INSTRUCTION(vcmpequb, 0x10000006, VXR, General, 0,
                     "Vector Compare Equal-to Unsigned Byte"),
    SCAN_INSTRUCTION(vcmpequh, 0x10000046, VXR, General, 0,
                     "Vector Compare Equal-to Unsigned Half Word"),
    SCAN_INSTRUCTION(vcmpequw, 0x10000086, VXR, General, 0,
                     "Vector Compare Equal-to Unsigned Word"),
    SCAN_INSTRUCTION(vcmpgefp, 0x100001C6, VXR, General, 0,
                     "Vector Compare Greater-Than-or-Equal-to Floating Point"),
    SCAN_INSTRUCTION(vcmpgtfp, 0x100002C6, VXR, General, 0,
                     "Vector Compare Greater-Than Floating Point"),
    SCAN_INSTRUCTION(vcmpgtsb, 0x10000306, VXR, General, 0,
                     "Vector Compare Greater-Than Signed Byte"),
    SCAN_INSTRUCTION(vcmpgtsh, 0x10000346, VXR, General, 0,
                     "Vector Compare Greater-Than Signed Half Word"),
    SCAN_INSTRUCTION(vcmpgtsw, 0x10000386, VXR, General, 0,
                     "Vector Compare Greater-Than Signed Word"),
    SCAN_INSTRUCTION(vcmpgtub, 0x10000206, VXR, General, 0,
                     "Vector Compare Greater-Than Unsigned Byte"),
    SCAN_INSTRUCTION(vcmpgtuh, 0x10000246, VXR, General, 0,
                     "Vector Compare Greater-Than Unsigned Half Word"),
    SCAN_INSTRUCTION(vcmpgtuw, 0x10000286, VXR, General, 0,
                     "Vector Compare Greater-Than Unsigned Word"),
    SCAN_INSTRUCTION(vmaddfp, 0x1000002E, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Add Floating Point"),
    SCAN_INSTRUCTION(
        vmhaddshs, 0x10000020, VXA, General, VXA_VD_VA_VB_VC,
        "Vector Multiply-High and Add Signed Signed Half Word Saturate"),
    SCAN_INSTRUCTION(
        vmhraddshs, 0x10000021, VXA, General, VXA_VD_VA_VB_VC,
        "Vector Multiply-High Round and Add Signed Signed Half Word Saturate"),
    SCAN_INSTRUCTION(vmladduhm, 0x10000022, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Low and Add Unsigned Half Word Modulo"),
    SCAN_INSTRUCTION(vmsummbm, 0x10000025, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Mixed-Sign Byte Modulo"),
    SCAN_INSTRUCTION(vmsumshm, 0x10000028, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Signed Half Word Modulo"),
    SCAN_INSTRUCTION(vmsumshs, 0x10000029, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Signed Half Word Saturate"),
    SCAN_INSTRUCTION(vmsumubm, 0x10000024, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Unsigned Byte Modulo"),
    SCAN_INSTRUCTION(vmsumuhm, 0x10000026, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Unsigned Half Word Modulo"),
    SCAN_INSTRUCTION(vmsumuhs, 0x10000027, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Multiply-Sum Unsigned Half Word Saturate"),
    SCAN_INSTRUCTION(vnmsubfp, 0x1000002F, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Negative Multiply-Subtract Floating Point"),
    SCAN_INSTRUCTION(vperm, 0x1000002B, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Permute"),
    SCAN_INSTRUCTION(vsel, 0x1000002A, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Conditional Select"),
    SCAN_INSTRUCTION(vsldoi, 0x1000002C, VXA, General, VXA_VD_VA_VB_VC,
                     "Vector Shift Left Double by Octet Immediate"),
    SCAN_INSTRUCTION(lvsl128, VX128_1(4, 3), VX128_1, General, VX1281_VD_RA0_RB,
                     "Load Vector128 for Shift Left"),
    SCAN_INSTRUCTION(lvsr128, VX128_1(4, 67), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 for Shift Right"),
    SCAN_INSTRUCTION(lvewx128, VX128_1(4, 131), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Element Word Indexed"),
    SCAN_INSTRUCTION(lvx128, VX128_1(4, 195), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Indexed"),
    SCAN_INSTRUCTION(stvewx128, VX128_1(4, 387), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Element Word Indexed"),
    SCAN_INSTRUCTION(stvx128, VX128_1(4, 451), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Indexed"),
    SCAN_INSTRUCTION(lvxl128, VX128_1(4, 707), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Left Indexed"),
    SCAN_INSTRUCTION(stvxl128, VX128_1(4, 963), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Indexed LRU"),
    SCAN_INSTRUCTION(lvlx128, VX128_1(4, 1027), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Left Indexed LRU"),
    SCAN_INSTRUCTION(lvrx128, VX128_1(4, 1091), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Right Indexed"),
    SCAN_INSTRUCTION(stvlx128, VX128_1(4, 1283), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Left Indexed"),
    SCAN_INSTRUCTION(stvrx128, VX128_1(4, 1347), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Right Indexed"),
    SCAN_INSTRUCTION(lvlxl128, VX128_1(4, 1539), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Indexed LRU"),
    SCAN_INSTRUCTION(lvrxl128, VX128_1(4, 1603), VX128_1, General,
                     VX1281_VD_RA0_RB, "Load Vector128 Right Indexed LRU"),
    SCAN_INSTRUCTION(stvlxl128, VX128_1(4, 1795), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Left Indexed LRU"),
    SCAN_INSTRUCTION(stvrxl128, VX128_1(4, 1859), VX128_1, General,
                     VX1281_VD_RA0_RB, "Store Vector128 Right Indexed LRU"),
    SCAN_INSTRUCTION(vsldoi128, VX128_5(4, 16), VX128_5, General, vsldoi128,
                     "Vector128 Shift Left Double by Octet Immediate"),
    SCAN_INSTRUCTION(vperm128, VX128_2(5, 0), VX128_2, General,
                     VX1282_VD_VA_VB_VC, "Vector128 Permute"),
    SCAN_INSTRUCTION(vaddfp128, VX128(5, 16), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Add Floating Point"),
    SCAN_INSTRUCTION(vsubfp128, VX128(5, 80), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Subtract Floating Point"),
    SCAN_INSTRUCTION(vmulfp128, VX128(5, 144), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Multiply Floating-Point"),
    SCAN_INSTRUCTION(vmaddfp128, VX128(5, 208), VX128, General,
                     VX128_VD_VA_VD_VB,
                     "Vector128 Multiply Add Floating Point"),
    SCAN_INSTRUCTION(vmaddcfp128, VX128(5, 272), VX128, General,
                     VX128_VD_VA_VD_VB,
                     "Vector128 Multiply Add Floating Point"),
    SCAN_INSTRUCTION(vnmsubfp128, VX128(5, 336), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Negative Multiply-Subtract Floating Point"),
    SCAN_INSTRUCTION(vmsum3fp128, VX128(5, 400), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Multiply Sum 3-way Floating Point"),
    SCAN_INSTRUCTION(vmsum4fp128, VX128(5, 464), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Multiply Sum 4-way Floating-Point"),
    SCAN_INSTRUCTION(vpkshss128, VX128(5, 512), VX128, General, 0,
                     "Vector128 Pack Signed Half Word Signed Saturate"),
    SCAN_INSTRUCTION(vand128, VX128(5, 528), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Logical AND"),
    SCAN_INSTRUCTION(vpkshus128, VX128(5, 576), VX128, General, 0,
                     "Vector128 Pack Signed Half Word Unsigned Saturate"),
    SCAN_INSTRUCTION(vandc128, VX128(5, 592), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Logical AND with Complement"),
    SCAN_INSTRUCTION(vpkswss128, VX128(5, 640), VX128, General, 0,
                     "Vector128 Pack Signed Word Signed Saturate"),
    SCAN_INSTRUCTION(vnor128, VX128(5, 656), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Logical NOR"),
    SCAN_INSTRUCTION(vpkswus128, VX128(5, 704), VX128, General, 0,
                     "Vector128 Pack Signed Word Unsigned Saturate"),
    SCAN_INSTRUCTION(vor128, VX128(5, 720), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Logical OR"),
    SCAN_INSTRUCTION(vpkuhum128, VX128(5, 768), VX128, General, 0,
                     "Vector128 Pack Unsigned Half Word Unsigned Modulo"),
    SCAN_INSTRUCTION(vxor128, VX128(5, 784), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Logical XOR"),
    SCAN_INSTRUCTION(vpkuhus128, VX128(5, 832), VX128, General, 0,
                     "Vector128 Pack Unsigned Half Word Unsigned Saturate"),
    SCAN_INSTRUCTION(vsel128, VX128(5, 848), VX128, General, 0,
                     "Vector128 Conditional Select"),
    SCAN_INSTRUCTION(vpkuwum128, VX128(5, 896), VX128, General, 0,
                     "Vector128 Pack Unsigned Word Unsigned Modulo"),
    SCAN_INSTRUCTION(vslo128, VX128(5, 912), VX128, General, 0,
                     "Vector128 Shift Left Octet"),
    SCAN_INSTRUCTION(vpkuwus128, VX128(5, 960), VX128, General, 0,
                     "Vector128 Pack Unsigned Word Unsigned Saturate"),
    SCAN_INSTRUCTION(vsro128, VX128(5, 976), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Shift Right Octet"),
    SCAN_INSTRUCTION(vpermwi128, VX128_P(6, 528), VX128_P, General, vpermwi128,
                     "Vector128 Permutate Word Immediate"),
    SCAN_INSTRUCTION(vcfpsxws128, VX128_3(6, 560), VX128_3, General,
                     VX1283_VD_VB_I,
                     "Vector128 Convert From Floating-Point to Signed "
                     "Fixed-Point Word Saturate"),
    SCAN_INSTRUCTION(vcfpuxws128, VX128_3(6, 624), VX128_3, General, 0,
                     "Vector128 Convert From Floating-Point to Unsigned "
                     "Fixed-Point Word Saturate"),
    SCAN_INSTRUCTION(
        vcsxwfp128, VX128_3(6, 688), VX128_3, General, VX1283_VD_VB_I,
        "Vector128 Convert From Signed Fixed-Point Word to Floating-Point"),
    SCAN_INSTRUCTION(
        vcuxwfp128, VX128_3(6, 752), VX128_3, General, 0,
        "Vector128 Convert From Unsigned Fixed-Point Word to Floating-Point"),
    SCAN_INSTRUCTION(
        vrfim128, VX128_3(6, 816), VX128_3, General, 0,
        "Vector128 Round to Floating-Point Integer toward -Infinity"),
    SCAN_INSTRUCTION(vrfin128, VX128_3(6, 880), VX128_3, General, vrfin128,
                     "Vector128 Round to Floating-Point Integer Nearest"),
    SCAN_INSTRUCTION(
        vrfip128, VX128_3(6, 944), VX128_3, General, 0,
        "Vector128 Round to Floating-Point Integer toward +Infinity"),
    SCAN_INSTRUCTION(vrfiz128, VX128_3(6, 1008), VX128_3, General, 0,
                     "Vector128 Round to Floating-Point Integer toward Zero"),
    SCAN_INSTRUCTION(
        vpkd3d128, VX128_4(6, 1552), VX128_4, General, 0,
        "Vector128 Pack D3Dtype, Rotate Left Immediate and Mask Insert"),
    SCAN_INSTRUCTION(vrefp128, VX128_3(6, 1584), VX128_3, General, 0,
                     "Vector128 Reciprocal Estimate Floating Point"),
    SCAN_INSTRUCTION(
        vrsqrtefp128, VX128_3(6, 1648), VX128_3, General, VX1283_VD_VB,
        "Vector128 Reciprocal Square Root Estimate Floating Point"),
    SCAN_INSTRUCTION(vexptefp128, VX128_3(6, 1712), VX128_3, General, 0,
                     "Vector128 Log2 Estimate Floating Point"),
    SCAN_INSTRUCTION(vlogefp128, VX128_3(6, 1776), VX128_3, General, 0,
                     "Vector128 Log2 Estimate Floating Point"),
    SCAN_INSTRUCTION(vrlimi128, VX128_4(6, 1808), VX128_4, General, vrlimi128,
                     "Vector128 Rotate Left Immediate and Mask Insert"),
    SCAN_INSTRUCTION(vspltw128, VX128_3(6, 1840), VX128_3, General,
                     VX1283_VD_VB_I, "Vector128 Splat Word"),
    SCAN_INSTRUCTION(vspltisw128, VX128_3(6, 1904), VX128_3, General,
                     VX1283_VD_VB_I, "Vector128 Splat Immediate Signed Word"),
    SCAN_INSTRUCTION(vupkd3d128, VX128_3(6, 2032), VX128_3, General,
                     VX1283_VD_VB_I, "Vector128 Unpack D3Dtype"),
    SCAN_INSTRUCTION(vcmpeqfp128, VX128_R(6, 0), VX128_R, General,
                     VX128_VD_VA_VB,
                     "Vector128 Compare Equal-to Floating Point"),
    SCAN_INSTRUCTION(vrlw128, VX128(6, 80), VX128, General, 0,
                     "Vector128 Rotate Left Word"),
    SCAN_INSTRUCTION(
        vcmpgefp128,
        VX128_R(6, 128), VX128_R, General, 0,
        "Vector128 Compare Greater-Than-or-Equal-to Floating Point"),
    SCAN_INSTRUCTION(vslw128, VX128(6, 208), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Shift Left Integer Word"),
    SCAN_INSTRUCTION(vcmpgtfp128, VX128_R(6, 256), VX128_R, General, 0,
                     "Vector128 Compare Greater-Than Floating-Point"),
    SCAN_INSTRUCTION(vsraw128, VX128(6, 336), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Shift Right Arithmetic Word"),
    SCAN_INSTRUCTION(vcmpbfp128, VX128_R(6, 384), VX128_R, General, 0,
                     "Vector128 Compare Bounds Floating Point"),
    SCAN_INSTRUCTION(vsrw128, VX128(6, 464), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Shift Right Word"),
    SCAN_INSTRUCTION(vcmpequw128, VX128_R(6, 512), VX128_R, General,
                     VX128_VD_VA_VB,
                     "Vector128 Compare Equal-to Unsigned Word"),
    SCAN_INSTRUCTION(vmaxfp128, VX128(6, 640), VX128, General, 0,
                     "Vector128 Maximum Floating Point"),
    SCAN_INSTRUCTION(vminfp128, VX128(6, 704), VX128, General, 0,
                     "Vector128 Minimum Floating Point"),
    SCAN_INSTRUCTION(vmrghw128, VX128(6, 768), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Merge High Word"),
    SCAN_INSTRUCTION(vmrglw128, VX128(6, 832), VX128, General, VX128_VD_VA_VB,
                     "Vector128 Merge Low Word"),
    SCAN_INSTRUCTION(vupkhsb128, VX128(6, 896), VX128, General, 0,
                     "Vector128 Unpack High Signed Byte"),
    SCAN_INSTRUCTION(vupklsb128, VX128(6, 960), VX128, General, 0,
                     "Vector128 Unpack Low Signed Byte"),
};
#undef OP
#undef VX128
#undef VX128_1
#undef VX128_2
#undef VX128_3
#undef VX128_4
#undef VX128_5
#undef VX128_P

#undef FLAG
#undef INSTRUCTION
#undef EMPTY

}  // namespace tables
}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FRONTEND_PPC_INSTR_TABLES_H_
