/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_INSTR_TABLES_H_
#define ALLOY_FRONTEND_PPC_PPC_INSTR_TABLES_H_

#include <alloy/frontend/ppc/ppc_instr.h>


namespace alloy {
namespace frontend {
namespace ppc {
namespace tables {


static InstrType** instr_table_prep(
    InstrType* unprep, int unprep_count, int a, int b) {
  int prep_count = (int)pow(2.0, b - a + 1);
  InstrType** prep = (InstrType**)xe_calloc(prep_count * sizeof(void*));
  for (int n = 0; n < unprep_count; n++) {
    int ordinal = XESELECTBITS(unprep[n].opcode, a, b);
    prep[ordinal] = &unprep[n];
  }
  return prep;
}

static InstrType** instr_table_prep_63(
    InstrType* unprep, int unprep_count, int a, int b) {
  // Special handling for A format instructions.
  int prep_count = (int)pow(2.0, b - a + 1);
  InstrType** prep = (InstrType**)xe_calloc(prep_count * sizeof(void*));
  for (int n = 0; n < unprep_count; n++) {
    int ordinal = XESELECTBITS(unprep[n].opcode, a, b);
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


#define EMPTY(slot) {0}
#define INSTRUCTION(name, opcode, format, type, flag)  { \
  opcode, \
  0, \
  kXEPPCInstrFormat##format, \
  kXEPPCInstrType##type, \
  flag, \
  #name, \
}
#define FLAG(t)               kXEPPCInstrFlag##t


// This table set is constructed from:
//   pem_64bit_v3.0.2005jul15.pdf, A.2
//   PowerISA_V2.06B_V2_PUBLIC.pdf

// Opcode = 4, index = bits 11-0 (6)
static InstrType instr_table_4_unprep[] = {
  // TODO: all of the vector ops
  INSTRUCTION(mfvscr,         0x10000604, VX      , General       , 0),
  INSTRUCTION(mtvscr,         0x10000644, VX      , General       , 0),
  INSTRUCTION(vaddcuw,        0x10000180, VX      , General       , 0),
  INSTRUCTION(vaddfp,         0x1000000A, VX      , General       , 0),
  INSTRUCTION(vaddsbs,        0x10000300, VX      , General       , 0),
  INSTRUCTION(vaddshs,        0x10000340, VX      , General       , 0),
  INSTRUCTION(vaddsws,        0x10000380, VX      , General       , 0),
  INSTRUCTION(vaddubm,        0x10000000, VX      , General       , 0),
  INSTRUCTION(vaddubs,        0x10000200, VX      , General       , 0),
  INSTRUCTION(vadduhm,        0x10000040, VX      , General       , 0),
  INSTRUCTION(vadduhs,        0x10000240, VX      , General       , 0),
  INSTRUCTION(vadduwm,        0x10000080, VX      , General       , 0),
  INSTRUCTION(vadduws,        0x10000280, VX      , General       , 0),
  INSTRUCTION(vand,           0x10000404, VX      , General       , 0),
  INSTRUCTION(vandc,          0x10000444, VX      , General       , 0),
  INSTRUCTION(vavgsb,         0x10000502, VX      , General       , 0),
  INSTRUCTION(vavgsh,         0x10000542, VX      , General       , 0),
  INSTRUCTION(vavgsw,         0x10000582, VX      , General       , 0),
  INSTRUCTION(vavgub,         0x10000402, VX      , General       , 0),
  INSTRUCTION(vavguh,         0x10000442, VX      , General       , 0),
  INSTRUCTION(vavguw,         0x10000482, VX      , General       , 0),
  INSTRUCTION(vcfsx,          0x1000034A, VX      , General       , 0),
  INSTRUCTION(vcfux,          0x1000030A, VX      , General       , 0),
  INSTRUCTION(vctsxs,         0x100003CA, VX      , General       , 0),
  INSTRUCTION(vctuxs,         0x1000038A, VX      , General       , 0),
  INSTRUCTION(vexptefp,       0x1000018A, VX      , General       , 0),
  INSTRUCTION(vlogefp,        0x100001CA, VX      , General       , 0),
  INSTRUCTION(vmaxfp,         0x1000040A, VX      , General       , 0),
  INSTRUCTION(vmaxsb,         0x10000102, VX      , General       , 0),
  INSTRUCTION(vmaxsh,         0x10000142, VX      , General       , 0),
  INSTRUCTION(vmaxsw,         0x10000182, VX      , General       , 0),
  INSTRUCTION(vmaxub,         0x10000002, VX      , General       , 0),
  INSTRUCTION(vmaxuh,         0x10000042, VX      , General       , 0),
  INSTRUCTION(vmaxuw,         0x10000082, VX      , General       , 0),
  INSTRUCTION(vminfp,         0x1000044A, VX      , General       , 0),
  INSTRUCTION(vminsb,         0x10000302, VX      , General       , 0),
  INSTRUCTION(vminsh,         0x10000342, VX      , General       , 0),
  INSTRUCTION(vminsw,         0x10000382, VX      , General       , 0),
  INSTRUCTION(vminub,         0x10000202, VX      , General       , 0),
  INSTRUCTION(vminuh,         0x10000242, VX      , General       , 0),
  INSTRUCTION(vminuw,         0x10000282, VX      , General       , 0),
  INSTRUCTION(vmrghb,         0x1000000C, VX      , General       , 0),
  INSTRUCTION(vmrghh,         0x1000004C, VX      , General       , 0),
  INSTRUCTION(vmrghw,         0x1000008C, VX      , General       , 0),
  INSTRUCTION(vmrglb,         0x1000010C, VX      , General       , 0),
  INSTRUCTION(vmrglh,         0x1000014C, VX      , General       , 0),
  INSTRUCTION(vmrglw,         0x1000018C, VX      , General       , 0),
  INSTRUCTION(vmulesb,        0x10000308, VX      , General       , 0),
  INSTRUCTION(vmulesh,        0x10000348, VX      , General       , 0),
  INSTRUCTION(vmuleub,        0x10000208, VX      , General       , 0),
  INSTRUCTION(vmuleuh,        0x10000248, VX      , General       , 0),
  INSTRUCTION(vmulosb,        0x10000108, VX      , General       , 0),
  INSTRUCTION(vmulosh,        0x10000148, VX      , General       , 0),
  INSTRUCTION(vmuloub,        0x10000008, VX      , General       , 0),
  INSTRUCTION(vmulouh,        0x10000048, VX      , General       , 0),
  INSTRUCTION(vnor,           0x10000504, VX      , General       , 0),
  INSTRUCTION(vor,            0x10000484, VX      , General       , 0),
  INSTRUCTION(vpkpx,          0x1000030E, VX      , General       , 0),
  INSTRUCTION(vpkshss,        0x1000018E, VX      , General       , 0),
  INSTRUCTION(vpkshus,        0x1000010E, VX      , General       , 0),
  INSTRUCTION(vpkswss,        0x100001CE, VX      , General       , 0),
  INSTRUCTION(vpkswus,        0x1000014E, VX      , General       , 0),
  INSTRUCTION(vpkuhum,        0x1000000E, VX      , General       , 0),
  INSTRUCTION(vpkuhus,        0x1000008E, VX      , General       , 0),
  INSTRUCTION(vpkuwum,        0x1000004E, VX      , General       , 0),
  INSTRUCTION(vpkuwus,        0x100000CE, VX      , General       , 0),
  INSTRUCTION(vrefp,          0x1000010A, VX      , General       , 0),
  INSTRUCTION(vrfim,          0x100002CA, VX      , General       , 0),
  INSTRUCTION(vrfin,          0x1000020A, VX      , General       , 0),
  INSTRUCTION(vrfip,          0x1000028A, VX      , General       , 0),
  INSTRUCTION(vrfiz,          0x1000024A, VX      , General       , 0),
  INSTRUCTION(vrlb,           0x10000004, VX      , General       , 0),
  INSTRUCTION(vrlh,           0x10000044, VX      , General       , 0),
  INSTRUCTION(vrlw,           0x10000084, VX      , General       , 0),
  INSTRUCTION(vrsqrtefp,      0x1000014A, VX      , General       , 0),
  INSTRUCTION(vsl,            0x100001C4, VX      , General       , 0),
  INSTRUCTION(vslb,           0x10000104, VX      , General       , 0),
  INSTRUCTION(vslh,           0x10000144, VX      , General       , 0),
  INSTRUCTION(vslo,           0x1000040C, VX      , General       , 0),
  INSTRUCTION(vslw,           0x10000184, VX      , General       , 0),
  INSTRUCTION(vspltb,         0x1000020C, VX      , General       , 0),
  INSTRUCTION(vsplth,         0x1000024C, VX      , General       , 0),
  INSTRUCTION(vspltisb,       0x1000030C, VX      , General       , 0),
  INSTRUCTION(vspltish,       0x1000034C, VX      , General       , 0),
  INSTRUCTION(vspltisw,       0x1000038C, VX      , General       , 0),
  INSTRUCTION(vspltw,         0x1000028C, VX      , General       , 0),
  INSTRUCTION(vsr,            0x100002C4, VX      , General       , 0),
  INSTRUCTION(vsrab,          0x10000304, VX      , General       , 0),
  INSTRUCTION(vsrah,          0x10000344, VX      , General       , 0),
  INSTRUCTION(vsraw,          0x10000384, VX      , General       , 0),
  INSTRUCTION(vsrb,           0x10000204, VX      , General       , 0),
  INSTRUCTION(vsrh,           0x10000244, VX      , General       , 0),
  INSTRUCTION(vsro,           0x1000044C, VX      , General       , 0),
  INSTRUCTION(vsrw,           0x10000284, VX      , General       , 0),
  INSTRUCTION(vsubcuw,        0x10000580, VX      , General       , 0),
  INSTRUCTION(vsubfp,         0x1000004A, VX      , General       , 0),
  INSTRUCTION(vsubsbs,        0x10000700, VX      , General       , 0),
  INSTRUCTION(vsubshs,        0x10000740, VX      , General       , 0),
  INSTRUCTION(vsubsws,        0x10000780, VX      , General       , 0),
  INSTRUCTION(vsububm,        0x10000400, VX      , General       , 0),
  INSTRUCTION(vsububs,        0x10000600, VX      , General       , 0),
  INSTRUCTION(vsubuhm,        0x10000440, VX      , General       , 0),
  INSTRUCTION(vsubuhs,        0x10000640, VX      , General       , 0),
  INSTRUCTION(vsubuwm,        0x10000480, VX      , General       , 0),
  INSTRUCTION(vsubuws,        0x10000680, VX      , General       , 0),
  INSTRUCTION(vsumsws,        0x10000788, VX      , General       , 0),
  INSTRUCTION(vsum2sws,       0x10000688, VX      , General       , 0),
  INSTRUCTION(vsum4sbs,       0x10000708, VX      , General       , 0),
  INSTRUCTION(vsum4shs,       0x10000648, VX      , General       , 0),
  INSTRUCTION(vsum4ubs,       0x10000608, VX      , General       , 0),
  INSTRUCTION(vupkhpx,        0x1000034E, VX      , General       , 0),
  INSTRUCTION(vupkhsb,        0x1000020E, VX      , General       , 0),
  INSTRUCTION(vupkhsh,        0x1000024E, VX      , General       , 0),
  INSTRUCTION(vupklpx,        0x100003CE, VX      , General       , 0),
  INSTRUCTION(vupklsb,        0x1000028E, VX      , General       , 0),
  INSTRUCTION(vupklsh,        0x100002CE, VX      , General       , 0),
  INSTRUCTION(vxor,           0x100004C4, VX      , General       , 0),
};
static InstrType** instr_table_4 = instr_table_prep(
    instr_table_4_unprep, XECOUNT(instr_table_4_unprep), 0, 11);

// Opcode = 19, index = bits 10-1 (10)
static InstrType instr_table_19_unprep[] = {
  INSTRUCTION(mcrf,           0x4C000000, XL , General        , 0),
  INSTRUCTION(bclrx,          0x4C000020, XL , BranchCond     , 0),
  INSTRUCTION(crnor,          0x4C000042, XL , General        , 0),
  INSTRUCTION(crandc,         0x4C000102, XL , General        , 0),
  INSTRUCTION(isync,          0x4C00012C, XL , General        , 0),
  INSTRUCTION(crxor,          0x4C000182, XL , General        , 0),
  INSTRUCTION(crnand,         0x4C0001C2, XL , General        , 0),
  INSTRUCTION(crand,          0x4C000202, XL , General        , 0),
  INSTRUCTION(creqv,          0x4C000242, XL , General        , 0),
  INSTRUCTION(crorc,          0x4C000342, XL , General        , 0),
  INSTRUCTION(cror,           0x4C000382, XL , General        , 0),
  INSTRUCTION(bcctrx,         0x4C000420, XL , BranchCond     , 0),
};
static InstrType** instr_table_19 = instr_table_prep(
    instr_table_19_unprep, XECOUNT(instr_table_19_unprep), 1, 10);

// Opcode = 30, index = bits 4-1 (4)
static InstrType instr_table_30_unprep[] = {
  // Decoding these instrunctions in this table is difficult because the
  // index bits are kind of random. This is special cased by an uber
  // instruction handler.
  INSTRUCTION(rld,            0x78000000, MD , General        , 0),
  // INSTRUCTION(rldiclx,        0x78000000, MD , General        , 0),
  // INSTRUCTION(rldicrx,        0x78000004, MD , General        , 0),
  // INSTRUCTION(rldicx,         0x78000008, MD , General        , 0),
  // INSTRUCTION(rldimix,        0x7800000C, MD , General        , 0),
  // INSTRUCTION(rldclx,         0x78000010, MDS, General        , 0),
  // INSTRUCTION(rldcrx,         0x78000012, MDS, General        , 0),
};
static InstrType** instr_table_30 = instr_table_prep(
    instr_table_30_unprep, XECOUNT(instr_table_30_unprep), 0, 0);

// Opcode = 31, index = bits 10-1 (10)
static InstrType instr_table_31_unprep[] = {
  INSTRUCTION(cmp,            0x7C000000, X  , General        , 0),
  INSTRUCTION(tw,             0x7C000008, X  , General        , 0),
  INSTRUCTION(lvsl,           0x7C00000C, X  , General        , 0),
  INSTRUCTION(lvebx,          0x7C00000E, X  , General        , 0),
  INSTRUCTION(subfcx,         0x7C000010, XO , General        , 0),
  INSTRUCTION(mulhdux,        0x7C000012, XO , General        , 0),
  INSTRUCTION(addcx,          0x7C000014, XO , General        , 0),
  INSTRUCTION(mulhwux,        0x7C000016, XO , General        , 0),
  INSTRUCTION(mfcr,           0x7C000026, X  , General        , 0),
  INSTRUCTION(lwarx,          0x7C000028, X  , General        , 0),
  INSTRUCTION(ldx,            0x7C00002A, X  , General        , 0),
  INSTRUCTION(lwzx,           0x7C00002E, X  , General        , 0),
  INSTRUCTION(slwx,           0x7C000030, X  , General        , 0),
  INSTRUCTION(cntlzwx,        0x7C000034, X  , General        , 0),
  INSTRUCTION(sldx,           0x7C000036, X  , General        , 0),
  INSTRUCTION(andx,           0x7C000038, X  , General        , 0),
  INSTRUCTION(cmpl,           0x7C000040, X  , General        , 0),
  INSTRUCTION(lvsr,           0x7C00004C, X  , General        , 0),
  INSTRUCTION(lvehx,          0x7C00004E, X  , General        , 0),
  INSTRUCTION(subfx,          0x7C000050, XO , General        , 0),
  INSTRUCTION(ldux,           0x7C00006A, X  , General        , 0),
  INSTRUCTION(dcbst,          0x7C00006C, X  , General        , 0),
  INSTRUCTION(lwzux,          0x7C00006E, X  , General        , 0),
  INSTRUCTION(cntlzdx,        0x7C000074, X  , General        , 0),
  INSTRUCTION(andcx,          0x7C000078, X  , General        , 0),
  INSTRUCTION(td,             0x7C000088, X  , General        , 0),
  INSTRUCTION(lvewx,          0x7C00008E, X  , General        , 0),
  INSTRUCTION(mulhdx,         0x7C000092, XO , General        , 0),
  INSTRUCTION(mulhwx,         0x7C000096, XO , General        , 0),
  INSTRUCTION(mfmsr,          0x7C0000A6, X  , General        , 0),
  INSTRUCTION(ldarx,          0x7C0000A8, X  , General        , 0),
  INSTRUCTION(dcbf,           0x7C0000AC, X  , General        , 0),
  INSTRUCTION(lbzx,           0x7C0000AE, X  , General        , 0),
  INSTRUCTION(lvx,            0x7C0000CE, X  , General        , 0),
  INSTRUCTION(negx,           0x7C0000D0, XO , General        , 0),
  INSTRUCTION(lbzux,          0x7C0000EE, X  , General        , 0),
  INSTRUCTION(norx,           0x7C0000F8, X  , General        , 0),
  INSTRUCTION(stvebx,         0x7C00010E, X  , General        , 0),
  INSTRUCTION(subfex,         0x7C000110, XO , General        , 0),
  INSTRUCTION(addex,          0x7C000114, XO , General        , 0),
  INSTRUCTION(mtcrf,          0x7C000120, XFX, General        , 0),
  INSTRUCTION(mtmsr,          0x7C000124, X  , General        , 0),
  INSTRUCTION(stdx,           0x7C00012A, X  , General        , 0),
  INSTRUCTION(stwcx,          0x7C00012D, X  , General        , 0),
  INSTRUCTION(stwx,           0x7C00012E, X  , General        , 0),
  INSTRUCTION(stvehx,         0x7C00014E, X  , General        , 0),
  INSTRUCTION(mtmsrd,         0x7C000164, X  , General        , 0),
  INSTRUCTION(stdux,          0x7C00016A, X  , General        , 0),
  INSTRUCTION(stwux,          0x7C00016E, X  , General        , 0),
  INSTRUCTION(stvewx,         0x7C00018E, X  , General        , 0),
  INSTRUCTION(subfzex,        0x7C000190, XO , General        , 0),
  INSTRUCTION(addzex,         0x7C000194, XO , General        , 0),
  INSTRUCTION(stdcx,          0x7C0001AD, X  , General        , 0),
  INSTRUCTION(stbx,           0x7C0001AE, X  , General        , 0),
  INSTRUCTION(stvx,           0x7C0001CE, X  , General        , 0),
  INSTRUCTION(subfmex,        0x7C0001D0, XO , General        , 0),
  INSTRUCTION(mulldx,         0x7C0001D2, XO , General        , 0),
  INSTRUCTION(addmex,         0x7C0001D4, XO , General        , 0),
  INSTRUCTION(mullwx,         0x7C0001D6, XO , General        , 0),
  INSTRUCTION(dcbtst,         0x7C0001EC, X  , General        , 0),
  INSTRUCTION(stbux,          0x7C0001EE, X  , General        , 0),
  INSTRUCTION(addx,           0x7C000214, XO , General        , 0),
  INSTRUCTION(dcbt,           0x7C00022C, X  , General        , 0),
  INSTRUCTION(lhzx,           0x7C00022E, X  , General        , 0),
  INSTRUCTION(eqvx,           0x7C000238, X  , General        , 0),
  INSTRUCTION(eciwx,          0x7C00026C, X  , General        , 0),
  INSTRUCTION(lhzux,          0x7C00026E, X  , General        , 0),
  INSTRUCTION(xorx,           0x7C000278, X  , General        , 0),
  INSTRUCTION(mfspr,          0x7C0002A6, XFX, General        , 0),
  INSTRUCTION(lwax,           0x7C0002AA, X  , General        , 0),
  INSTRUCTION(lhax,           0x7C0002AE, X  , General        , 0),
  INSTRUCTION(lvxl,           0x7C0002CE, X  , General        , 0),
  INSTRUCTION(mftb,           0x7C0002E6, XFX, General        , 0),
  INSTRUCTION(lwaux,          0x7C0002EA, X  , General        , 0),
  INSTRUCTION(lhaux,          0x7C0002EE, X  , General        , 0),
  INSTRUCTION(sthx,           0x7C00032E, X  , General        , 0),
  INSTRUCTION(orcx,           0x7C000338, X  , General        , 0),
  INSTRUCTION(ecowx,          0x7C00036C, X  , General        , 0),
  INSTRUCTION(sthux,          0x7C00036E, X  , General        , 0),
  INSTRUCTION(orx,            0x7C000378, X  , General        , 0),
  INSTRUCTION(divdux,         0x7C000392, XO , General        , 0),
  INSTRUCTION(divwux,         0x7C000396, XO , General        , 0),
  INSTRUCTION(mtspr,          0x7C0003A6, XFX, General        , 0),
  INSTRUCTION(nandx,          0x7C0003B8, X  , General        , 0),
  INSTRUCTION(stvxl,          0x7C0003CE, X  , General        , 0),
  INSTRUCTION(divdx,          0x7C0003D2, XO , General        , 0),
  INSTRUCTION(divwx,          0x7C0003D6, XO , General        , 0),
  INSTRUCTION(lvlx,           0x7C00040E, X  , General        , 0),
  INSTRUCTION(ldbrx,          0x7C000428, X  , General        , 0),
  INSTRUCTION(lswx,           0x7C00042A, X  , General        , 0),
  INSTRUCTION(lwbrx,          0x7C00042C, X  , General        , 0),
  INSTRUCTION(lfsx,           0x7C00042E, X  , General        , 0),
  INSTRUCTION(srwx,           0x7C000430, X  , General        , 0),
  INSTRUCTION(srdx,           0x7C000436, X  , General        , 0),
  INSTRUCTION(lfsux,          0x7C00046E, X  , General        , 0),
  INSTRUCTION(lswi,           0x7C0004AA, X  , General        , 0),
  INSTRUCTION(sync,           0x7C0004AC, X  , General        , 0),
  INSTRUCTION(lfdx,           0x7C0004AE, X  , General        , 0),
  INSTRUCTION(lfdux,          0x7C0004EE, X  , General        , 0),
  INSTRUCTION(stdbrx,         0x7C000528, X  , General        , 0),
  INSTRUCTION(stswx,          0x7C00052A, X  , General        , 0),
  INSTRUCTION(stwbrx,         0x7C00052C, X  , General        , 0),
  INSTRUCTION(stfsx,          0x7C00052E, X  , General        , 0),
  INSTRUCTION(stfsux,         0x7C00056E, X  , General        , 0),
  INSTRUCTION(stswi,          0x7C0005AA, X  , General        , 0),
  INSTRUCTION(stfdx,          0x7C0005AE, X  , General        , 0),
  INSTRUCTION(stfdux,         0x7C0005EE, X  , General        , 0),
  INSTRUCTION(lhbrx,          0x7C00062C, X  , General        , 0),
  INSTRUCTION(srawx,          0x7C000630, X  , General        , 0),
  INSTRUCTION(sradx,          0x7C000634, X  , General        , 0),
  INSTRUCTION(srawix,         0x7C000670, X  , General        , 0),
  INSTRUCTION(sradix,         0x7C000674, XS , General        , 0), // TODO
  INSTRUCTION(eieio,          0x7C0006AC, X  , General        , 0),
  INSTRUCTION(sthbrx,         0x7C00072C, X  , General        , 0),
  INSTRUCTION(extshx,         0x7C000734, X  , General        , 0),
  INSTRUCTION(extsbx,         0x7C000774, X  , General        , 0),
  INSTRUCTION(icbi,           0x7C0007AC, X  , General        , 0),
  INSTRUCTION(stfiwx,         0x7C0007AE, X  , General        , 0),
  INSTRUCTION(extswx,         0x7C0007B4, X  , General        , 0),
  INSTRUCTION(dcbz,           0x7C0007EC, X  , General        , 0), // 0x7C2007EC = DCBZ128
  INSTRUCTION(dst,            0x7C0002AC, XDSS, General       , 0),
  INSTRUCTION(dstst,          0x7C0002EC, XDSS, General       , 0),
  INSTRUCTION(dss,            0x7C00066C, XDSS, General       , 0),
  INSTRUCTION(lvebx,          0x7C00000E, X  , General        , 0),
  INSTRUCTION(lvehx,          0x7C00004E, X  , General        , 0),
  INSTRUCTION(lvewx,          0x7C00008E, X  , General        , 0),
  INSTRUCTION(lvsl,           0x7C00000C, X  , General        , 0),
  INSTRUCTION(lvsr,           0x7C00004C, X  , General        , 0),
  INSTRUCTION(lvx,            0x7C0000CE, X  , General        , 0),
  INSTRUCTION(lvxl,           0x7C0002CE, X  , General        , 0),
  INSTRUCTION(stvebx,         0x7C00010E, X  , General        , 0),
  INSTRUCTION(stvehx,         0x7C00014E, X  , General        , 0),
  INSTRUCTION(stvewx,         0x7C00018E, X  , General        , 0),
  INSTRUCTION(stvx,           0x7C0001CE, X  , General        , 0),
  INSTRUCTION(stvxl,          0x7C0003CE, X  , General        , 0),
  INSTRUCTION(lvlx,           0x7C00040E, X  , General        , 0),
  INSTRUCTION(lvlxl,          0x7C00060E, X  , General        , 0),
  INSTRUCTION(lvrx,           0x7C00044E, X  , General        , 0),
  INSTRUCTION(lvrxl,          0x7C00064E, X  , General        , 0),
  INSTRUCTION(stvlx,          0x7C00050E, X  , General        , 0),
  INSTRUCTION(stvlxl,         0x7C00070E, X  , General        , 0),
  INSTRUCTION(stvrx,          0x7C00054E, X  , General        , 0),
  INSTRUCTION(stvrxl,         0x7C00074E, X  , General        , 0),
};
static InstrType** instr_table_31 = instr_table_prep(
    instr_table_31_unprep, XECOUNT(instr_table_31_unprep), 1, 10);

// Opcode = 58, index = bits 1-0 (2)
static InstrType instr_table_58_unprep[] = {
  INSTRUCTION(ld,             0xE8000000, DS , General        , 0),
  INSTRUCTION(ldu,            0xE8000001, DS , General        , 0),
  INSTRUCTION(lwa,            0xE8000002, DS , General        , 0),
};
static InstrType** instr_table_58 = instr_table_prep(
    instr_table_58_unprep, XECOUNT(instr_table_58_unprep), 0, 1);

// Opcode = 59, index = bits 5-1 (5)
static InstrType instr_table_59_unprep[] = {
  INSTRUCTION(fdivsx,         0xEC000024, A  , General        , 0),
  INSTRUCTION(fsubsx,         0xEC000028, A  , General        , 0),
  INSTRUCTION(faddsx,         0xEC00002A, A  , General        , 0),
  INSTRUCTION(fsqrtsx,        0xEC00002C, A  , General        , 0),
  INSTRUCTION(fresx,          0xEC000030, A  , General        , 0),
  INSTRUCTION(fmulsx,         0xEC000032, A  , General        , 0),
  INSTRUCTION(fmsubsx,        0xEC000038, A  , General        , 0),
  INSTRUCTION(fmaddsx,        0xEC00003A, A  , General        , 0),
  INSTRUCTION(fnmsubsx,       0xEC00003C, A  , General        , 0),
  INSTRUCTION(fnmaddsx,       0xEC00003E, A  , General        , 0),
};
static InstrType** instr_table_59 = instr_table_prep(
    instr_table_59_unprep, XECOUNT(instr_table_59_unprep), 1, 5);

// Opcode = 62, index = bits 1-0 (2)
static InstrType instr_table_62_unprep[] = {
  INSTRUCTION(std,            0xF8000000, DS , General        , 0),
  INSTRUCTION(stdu,           0xF8000001, DS , General        , 0),
};
static InstrType** instr_table_62 = instr_table_prep(
    instr_table_62_unprep, XECOUNT(instr_table_62_unprep), 0, 1);

// Opcode = 63, index = bits 10-1 (10)
// NOTE: the A format instructions need some special handling because
//       they only use 6bits to identify their index.
static InstrType instr_table_63_unprep[] = {
  INSTRUCTION(fcmpu,          0xFC000000, X  , General        , 0),
  INSTRUCTION(frspx,          0xFC000018, X  , General        , 0),
  INSTRUCTION(fctiwx,         0xFC00001C, X  , General        , 0),
  INSTRUCTION(fctiwzx,        0xFC00001E, X  , General        , 0),
  INSTRUCTION(fdivx,          0xFC000024, A  , General        , 0),
  INSTRUCTION(fsubx,          0xFC000028, A  , General        , 0),
  INSTRUCTION(faddx,          0xFC00002A, A  , General        , 0),
  INSTRUCTION(fsqrtx,         0xFC00002C, A  , General        , 0),
  INSTRUCTION(fselx,          0xFC00002E, A  , General        , 0),
  INSTRUCTION(fmulx,          0xFC000032, A  , General        , 0),
  INSTRUCTION(frsqrtex,       0xFC000034, A  , General        , 0),
  INSTRUCTION(fmsubx,         0xFC000038, A  , General        , 0),
  INSTRUCTION(fmaddx,         0xFC00003A, A  , General        , 0),
  INSTRUCTION(fnmsubx,        0xFC00003C, A  , General        , 0),
  INSTRUCTION(fnmaddx,        0xFC00003E, A  , General        , 0),
  INSTRUCTION(fcmpo,          0xFC000040, X  , General        , 0),
  INSTRUCTION(mtfsb1x,        0xFC00004C, X  , General        , 0),
  INSTRUCTION(fnegx,          0xFC000050, X  , General        , 0),
  INSTRUCTION(mcrfs,          0xFC000080, X  , General        , 0),
  INSTRUCTION(mtfsb0x,        0xFC00008C, X  , General        , 0),
  INSTRUCTION(fmrx,           0xFC000090, X  , General        , 0),
  INSTRUCTION(mtfsfix,        0xFC00010C, X  , General        , 0),
  INSTRUCTION(fnabsx,         0xFC000110, X  , General        , 0),
  INSTRUCTION(fabsx,          0xFC000210, X  , General        , 0),
  INSTRUCTION(mffsx,          0xFC00048E, X  , General        , 0),
  INSTRUCTION(mtfsfx,         0xFC00058E, XFL, General        , 0),
  INSTRUCTION(fctidx,         0xFC00065C, X  , General        , 0),
  INSTRUCTION(fctidzx,        0xFC00065E, X  , General        , 0),
  INSTRUCTION(fcfidx,         0xFC00069C, X  , General        , 0),
};
static InstrType** instr_table_63 = instr_table_prep_63(
    instr_table_63_unprep, XECOUNT(instr_table_63_unprep), 1, 10);

// Main table, index = bits 31-26 (6) : (code >> 26)
static InstrType instr_table_unprep[64] = {
  INSTRUCTION(tdi,            0x08000000, D  , General        , 0),
  INSTRUCTION(twi,            0x0C000000, D  , General        , 0),
  INSTRUCTION(mulli,          0x1C000000, D  , General        , 0),
  INSTRUCTION(subficx,        0x20000000, D  , General        , 0),
  INSTRUCTION(cmpli,          0x28000000, D  , General        , 0),
  INSTRUCTION(cmpi,           0x2C000000, D  , General        , 0),
  INSTRUCTION(addic,          0x30000000, D  , General        , 0),
  INSTRUCTION(addicx,         0x34000000, D  , General        , 0),
  INSTRUCTION(addi,           0x38000000, D  , General        , 0),
  INSTRUCTION(addis,          0x3C000000, D  , General        , 0),
  INSTRUCTION(bcx,            0x40000000, B  , BranchCond     , 0),
  INSTRUCTION(sc,             0x44000002, SC , Syscall        , 0),
  INSTRUCTION(bx,             0x48000000, I  , BranchAlways   , 0),
  INSTRUCTION(rlwimix,        0x50000000, M  , General        , 0),
  INSTRUCTION(rlwinmx,        0x54000000, M  , General        , 0),
  INSTRUCTION(rlwnmx,         0x5C000000, M  , General        , 0),
  INSTRUCTION(ori,            0x60000000, D  , General        , 0),
  INSTRUCTION(oris,           0x64000000, D  , General        , 0),
  INSTRUCTION(xori,           0x68000000, D  , General        , 0),
  INSTRUCTION(xoris,          0x6C000000, D  , General        , 0),
  INSTRUCTION(andix,          0x70000000, D  , General        , 0),
  INSTRUCTION(andisx,         0x74000000, D  , General        , 0),
  INSTRUCTION(lwz,            0x80000000, D  , General        , 0),
  INSTRUCTION(lwzu,           0x84000000, D  , General        , 0),
  INSTRUCTION(lbz,            0x88000000, D  , General        , 0),
  INSTRUCTION(lbzu,           0x8C000000, D  , General        , 0),
  INSTRUCTION(stw,            0x90000000, D  , General        , 0),
  INSTRUCTION(stwu,           0x94000000, D  , General        , 0),
  INSTRUCTION(stb,            0x98000000, D  , General        , 0),
  INSTRUCTION(stbu,           0x9C000000, D  , General        , 0),
  INSTRUCTION(lhz,            0xA0000000, D  , General        , 0),
  INSTRUCTION(lhzu,           0xA4000000, D  , General        , 0),
  INSTRUCTION(lha,            0xA8000000, D  , General        , 0),
  INSTRUCTION(lhau,           0xAC000000, D  , General        , 0),
  INSTRUCTION(sth,            0xB0000000, D  , General        , 0),
  INSTRUCTION(sthu,           0xB4000000, D  , General        , 0),
  INSTRUCTION(lmw,            0xB8000000, D  , General        , 0),
  INSTRUCTION(stmw,           0xBC000000, D  , General        , 0),
  INSTRUCTION(lfs,            0xC0000000, D  , General        , 0),
  INSTRUCTION(lfsu,           0xC4000000, D  , General        , 0),
  INSTRUCTION(lfd,            0xC8000000, D  , General        , 0),
  INSTRUCTION(lfdu,           0xCC000000, D  , General        , 0),
  INSTRUCTION(stfs,           0xD0000000, D  , General        , 0),
  INSTRUCTION(stfsu,          0xD4000000, D  , General        , 0),
  INSTRUCTION(stfd,           0xD8000000, D  , General        , 0),
  INSTRUCTION(stfdu,          0xDC000000, D  , General        , 0),
};
static InstrType** instr_table = instr_table_prep(
    instr_table_unprep, XECOUNT(instr_table_unprep), 26, 31);

// Altivec instructions.
// TODO(benvanik): build a table like the other instructions.
// This table is looked up via linear scan of opcodes.
#define SCAN_INSTRUCTION(name, opcode, format, type, flag)  { \
  opcode, \
  kXEPPCInstrMask##format, \
  kXEPPCInstrFormat##format, \
  kXEPPCInstrType##type, \
  flag, \
  #name, \
}
#define OP(x)             ((((uint32_t)(x)) & 0x3f) << 26)
#define VX128(op, xop)    (OP(op) | (((uint32_t)(xop)) & 0x3d0))
#define VX128_1(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x7f3))
#define VX128_2(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x210))
#define VX128_3(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x7f0))
#define VX128_4(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x730))
#define VX128_5(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x10))
#define VX128_P(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x630))
#define VX128_R(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x390))
static InstrType instr_table_scan[] = {
  SCAN_INSTRUCTION(vcmpbfp,        0x100003C6, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpeqfp,       0x100000C6, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpequb,       0x10000006, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpequh,       0x10000046, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpequw,       0x10000086, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgefp,       0x100001C6, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtfp,       0x100002C6, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtsb,       0x10000306, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtsh,       0x10000346, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtsw,       0x10000386, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtub,       0x10000206, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtuh,       0x10000246, VXR     , General       , 0),
  SCAN_INSTRUCTION(vcmpgtuw,       0x10000286, VXR     , General       , 0),
  SCAN_INSTRUCTION(vmaddfp,        0x1000002E, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmhaddshs,      0x10000020, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmhraddshs,     0x10000021, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmladduhm,      0x10000022, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsummbm,       0x10000025, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsumshm,       0x10000028, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsumshs,       0x10000029, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsumubm,       0x10000024, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsumuhm,       0x10000026, VXA     , General       , 0),
  SCAN_INSTRUCTION(vmsumuhs,       0x10000027, VXA     , General       , 0),
  SCAN_INSTRUCTION(vnmsubfp,       0x1000002F, VXA     , General       , 0),
  SCAN_INSTRUCTION(vperm,          0x1000002B, VXA     , General       , 0),
  SCAN_INSTRUCTION(vsel,           0x1000002A, VXA     , General       , 0),
  SCAN_INSTRUCTION(vsldoi,         0x1000002C, VXA     , General       , 0),
  SCAN_INSTRUCTION(lvsl128,        VX128_1(4, 3),    VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvsr128,        VX128_1(4, 67),   VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvewx128,       VX128_1(4, 131),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvx128,         VX128_1(4, 195),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvewx128,      VX128_1(4, 387),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvx128,        VX128_1(4, 451),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvxl128,        VX128_1(4, 707),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvxl128,       VX128_1(4, 963),  VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvlx128,        VX128_1(4, 1027), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvrx128,        VX128_1(4, 1091), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvlx128,       VX128_1(4, 1283), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvrx128,       VX128_1(4, 1347), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvlxl128,       VX128_1(4, 1539), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(lvrxl128,       VX128_1(4, 1603), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvlxl128,      VX128_1(4, 1795), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(stvrxl128,      VX128_1(4, 1859), VX128_1 , General       , 0),
  SCAN_INSTRUCTION(vsldoi128,      VX128_5(4, 16),   VX128_5 , General       , 0),
  SCAN_INSTRUCTION(vperm128,       VX128_2(5, 0),    VX128_2 , General       , 0),
  SCAN_INSTRUCTION(vaddfp128,      VX128(5, 16),     VX128   , General       , 0),
  SCAN_INSTRUCTION(vsubfp128,      VX128(5,  80),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmulfp128,      VX128(5, 144),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmaddfp128,     VX128(5, 208),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmaddcfp128,    VX128(5, 272),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vnmsubfp128,    VX128(5, 336),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmsum3fp128,    VX128(5, 400),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmsum4fp128,    VX128(5, 464),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkshss128,     VX128(5, 512),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vand128,        VX128(5, 528),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkshus128,     VX128(5, 576),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vandc128,       VX128(5, 592),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkswss128,     VX128(5, 640),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vnor128,        VX128(5, 656),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkswus128,     VX128(5, 704),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vor128,         VX128(5, 720),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkuhum128,     VX128(5, 768),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vxor128,        VX128(5, 784),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkuhus128,     VX128(5, 832),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vsel128,        VX128(5, 848),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkuwum128,     VX128(5, 896),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vslo128,        VX128(5, 912),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpkuwus128,     VX128(5, 960),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vsro128,        VX128(5, 976),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vpermwi128,     VX128_P(6, 528),  VX128_P , General       , 0),
  SCAN_INSTRUCTION(vcfpsxws128,    VX128_3(6, 560),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vcfpuxws128,    VX128_3(6, 624),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vcsxwfp128,     VX128_3(6, 688),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vcuxwfp128,     VX128_3(6, 752),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrfim128,       VX128_3(6, 816),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrfin128,       VX128_3(6, 880),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrfip128,       VX128_3(6, 944),  VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrfiz128,       VX128_3(6, 1008), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vpkd3d128,      VX128_4(6, 1552), VX128_4 , General       , 0),
  SCAN_INSTRUCTION(vrefp128,       VX128_3(6, 1584), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrsqrtefp128,   VX128_3(6, 1648), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vexptefp128,    VX128_3(6, 1712), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vlogefp128,     VX128_3(6, 1776), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vrlimi128,      VX128_4(6, 1808), VX128_4 , General       , 0),
  SCAN_INSTRUCTION(vspltw128,      VX128_3(6, 1840), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vspltisw128,    VX128_3(6, 1904), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vupkd3d128,     VX128_3(6, 2032), VX128_3 , General       , 0),
  SCAN_INSTRUCTION(vcmpeqfp128,    VX128_R(6, 0),    VX128_R , General       , 0),
  SCAN_INSTRUCTION(vrlw128,        VX128(6, 80),     VX128   , General       , 0),
  SCAN_INSTRUCTION(vcmpgefp128,    VX128_R(6, 128),  VX128_R , General       , 0),
  SCAN_INSTRUCTION(vslw128,        VX128(6, 208),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vcmpgtfp128,    VX128_R(6, 256),  VX128_R , General       , 0),
  SCAN_INSTRUCTION(vsraw128,       VX128(6, 336),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vcmpbfp128,     VX128_R(6, 384),  VX128_R , General       , 0),
  SCAN_INSTRUCTION(vsrw128,        VX128(6, 464),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vcmpequw128,    VX128_R(6, 512),  VX128_R , General       , 0),
  SCAN_INSTRUCTION(vmaxfp128,      VX128(6, 640),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vminfp128,      VX128(6, 704),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmrghw128,      VX128(6, 768),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vmrglw128,      VX128(6, 832),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vupkhsb128,     VX128(6, 896),    VX128   , General       , 0),
  SCAN_INSTRUCTION(vupklsb128,     VX128(6, 960),    VX128   , General       , 0),
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
}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_INSTR_TABLES_H_
