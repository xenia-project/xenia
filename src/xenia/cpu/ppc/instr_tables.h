/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_INSTR_TABLE_H_
#define XENIA_CPU_PPC_INSTR_TABLE_H_

#include <xenia/cpu/ppc/instr.h>


namespace xe {
namespace cpu {
namespace ppc {
namespace tables {


static InstrType* instr_table_prep(
    InstrType* unprep, int unprep_count, int a, int b) {
  int prep_count = (int)pow(2, b - a + 1);
  InstrType* prep = (InstrType*)xe_calloc(prep_count * sizeof(InstrType));
  for (int n = 0; n < unprep_count; n++) {
    int ordinal = XESELECTBITS(unprep[n].opcode, a, b);
    prep[ordinal] = unprep[n];
  }
  return prep;
}


#define EMPTY(slot) {0}
#define INSTRUCTION(name, opcode, format, type, flag)  { \
  opcode, \
  kXEPPCInstrFormat##format, \
  kXEPPCInstrType##type, \
  flag, \
  #name, \
}
#define FLAG(t)               kXEPPCInstrFlag##t


// This table set is constructed from:
//   pem_64bit_v3.0.2005jul15.pdf, A.2
//   PowerISA_V2.06B_V2_PUBLIC.pdf

// Opcode = 4, index = bits 5-0 (6)
static InstrType instr_table_4_unprep[] = {
  // TODO: all of the vector ops
  INSTRUCTION(vperm,          0x1000002B, VA , General        , 0),
};
static InstrType* instr_table_4 = instr_table_prep(
    instr_table_4_unprep, XECOUNT(instr_table_4_unprep), 0, 5);

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
static InstrType* instr_table_19 = instr_table_prep(
    instr_table_19_unprep, XECOUNT(instr_table_19_unprep), 1, 10);

// Opcode = 30, index = bits 4-1 (4)
static InstrType instr_table_30_unprep[] = {
  INSTRUCTION(rldiclx,        0x78000000, MD , General        , 0),
  INSTRUCTION(rldicrx,        0x78000004, MD , General        , 0),
  INSTRUCTION(rldicx,         0x78000008, MD , General        , 0),
  INSTRUCTION(rldimix,        0x7800000C, MD , General        , 0),
  INSTRUCTION(rldclx,         0x78000010, MDS, General        , 0),
  INSTRUCTION(rldcrx,         0x78000012, MDS, General        , 0),
};
static InstrType* instr_table_30 = instr_table_prep(
    instr_table_30_unprep, XECOUNT(instr_table_30_unprep), 1, 4);

// Opcode = 31, index = bits 10-1 (10)
static InstrType instr_table_31_unprep[] = {
  INSTRUCTION(cmp,            0x7C000000, X  , General        , 0),
  INSTRUCTION(tw,             0x7C000008, X  , General        , 0),
  INSTRUCTION(lvsl,           0x7C00000C, X  , General        , 0),
  INSTRUCTION(lvebx,          0x7C00000E, X  , General        , 0),
  INSTRUCTION(subfcx,         0x7C000010, XO , General        , 0),
  INSTRUCTION(mulhdux,        0x7C000012, XO , General        , 0),
  INSTRUCTION(addcx,          0X7C000014, XO , General        , 0),
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
  INSTRUCTION(stdx,           0x7C00012A, X  , General        , 0),
  INSTRUCTION(stwcx,          0x7C00012D, X  , General        , 0),
  INSTRUCTION(stwx,           0x7C00012E, X  , General        , 0),
  INSTRUCTION(stvehx,         0x7C00014E, X  , General        , 0),
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
};
static InstrType* instr_table_31 = instr_table_prep(
    instr_table_31_unprep, XECOUNT(instr_table_31_unprep), 1, 10);

// Opcode = 58, index = bits 1-0 (2)
static InstrType instr_table_58_unprep[] = {
  INSTRUCTION(ld,             0xE8000000, DS , General        , 0),
  INSTRUCTION(ldu,            0xE8000001, DS , General        , 0),
  INSTRUCTION(lwa,            0xE8000002, DS , General        , 0),
};
static InstrType* instr_table_58 = instr_table_prep(
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
static InstrType* instr_table_59 = instr_table_prep(
    instr_table_59_unprep, XECOUNT(instr_table_59_unprep), 1, 5);

// Opcode = 62, index = bits 1-0 (2)
static InstrType instr_table_62_unprep[] = {
  INSTRUCTION(std,            0xF8000000, DS , General        , 0),
  INSTRUCTION(stdu,           0xF8000001, DS , General        , 0),
};
static InstrType* instr_table_62 = instr_table_prep(
    instr_table_62_unprep, XECOUNT(instr_table_62_unprep), 0, 1);

// Opcode = 63, index = bits 10-1 (10)
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
static InstrType* instr_table_63 = instr_table_prep(
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
static InstrType* instr_table = instr_table_prep(
    instr_table_unprep, XECOUNT(instr_table_unprep), 26, 31);


#undef FLAG
#undef INSTRUCTION
#undef EMPTY


}  // namespace tables
}  // namespace ppc
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PPC_INSTR_TABLE_H_
