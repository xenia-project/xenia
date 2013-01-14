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


static xe_ppc_instr_type_t *xe_ppc_instr_table_prep(
    xe_ppc_instr_type_t *unprep, int unprep_count, int a, int b) {
  int prep_count = pow(2, b - a + 1);
  xe_ppc_instr_type_t *prep = (xe_ppc_instr_type_t*)xe_calloc(
      prep_count * sizeof(xe_ppc_instr_type_t));
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
static xe_ppc_instr_type_t xe_ppc_instr_table_4_unprep[] = {
  // TODO: all of the vector ops
  INSTRUCTION(VPERM,          0x1000002B, VA , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_4 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_4_unprep, XECOUNT(xe_ppc_instr_table_4_unprep), 0, 5);

// Opcode = 19, index = bits 10-1 (10)
static xe_ppc_instr_type_t xe_ppc_instr_table_19_unprep[] = {
  INSTRUCTION(MCRF,           0x4C000000, XL , General        , 0),
  INSTRUCTION(BCLRx,          0x4C000020, XL , BranchCond     , 0),
  INSTRUCTION(CRNOR,          0x4C000042, XL , General        , 0),
  INSTRUCTION(CRANDC,         0x4C000102, XL , General        , 0),
  INSTRUCTION(ISYNC,          0x4C00012C, XL , General        , 0),
  INSTRUCTION(CRXOR,          0x4C000182, XL , General        , 0),
  INSTRUCTION(CRNAND,         0x4C0001C2, XL , General        , 0),
  INSTRUCTION(CRAND,          0x4C000202, XL , General        , 0),
  INSTRUCTION(CREQV,          0x4C000242, XL , General        , 0),
  INSTRUCTION(CRORC,          0x4C000342, XL , General        , 0),
  INSTRUCTION(CROR,           0x4C000382, XL , General        , 0),
  INSTRUCTION(BCCTRx,         0x4C000420, XL , BranchCond     , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_19 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_19_unprep, XECOUNT(xe_ppc_instr_table_19_unprep), 1, 10);

// Opcode = 30, index = bits 5-1 (5)
static xe_ppc_instr_type_t xe_ppc_instr_table_30_unprep[] = {
  INSTRUCTION(RLDICLx,        0x78000000, MD , General        , 0),
  INSTRUCTION(RLDICRx,        0x78000004, MD , General        , 0),
  INSTRUCTION(RLDICx,         0x78000008, MD , General        , 0),
  INSTRUCTION(RLDIMIx,        0x7800000C, MD , General        , 0),
  INSTRUCTION(RLDCLx,         0x78000010, MDS, General        , 0),
  INSTRUCTION(RLDCRx,         0x78000012, MDS, General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_30 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_30_unprep, XECOUNT(xe_ppc_instr_table_30_unprep), 1, 5);

// Opcode = 31, index = bits 10-1 (10)
static xe_ppc_instr_type_t xe_ppc_instr_table_31_unprep[] = {
  INSTRUCTION(CMP,            0x7C000000, X  , General        , 0),
  INSTRUCTION(TW,             0x7C000008, X  , General        , 0),
  INSTRUCTION(LVSL,           0x7C00000C, X  , General        , 0),
  INSTRUCTION(LVEBX,          0x7C00000E, X  , General        , 0),
  INSTRUCTION(SUBFCx,         0x7C000010, XO , General        , 0),
  INSTRUCTION(MULHDUx,        0x7C000012, XO , General        , 0),
  INSTRUCTION(ADDCx,          0X7C000014, XO , General        , 0),
  INSTRUCTION(MULHWUx,        0x7C000016, XO , General        , 0),
  INSTRUCTION(MFCR,           0x7C000026, X  , General        , 0),
  INSTRUCTION(LWARx,          0x7C000028, X  , General        , 0),
  INSTRUCTION(LDx,            0x7C00002A, X  , General        , 0),
  INSTRUCTION(LWZx,           0x7C00002E, X  , General        , 0),
  INSTRUCTION(SLWx,           0x7C000030, X  , General        , 0),
  INSTRUCTION(CNTLZWx,        0x7C000034, X  , General        , 0),
  INSTRUCTION(SLDx,           0x7C000036, X  , General        , 0),
  INSTRUCTION(ANDx,           0x7C000038, X  , General        , 0),
  INSTRUCTION(CMPL,           0x7C000040, X  , General        , 0),
  INSTRUCTION(LVSR,           0x7C00004C, X  , General        , 0),
  INSTRUCTION(LVEHX,          0x7C00004E, X  , General        , 0),
  INSTRUCTION(SUBFx,          0x7C000050, XO , General        , 0),
  INSTRUCTION(LDUx,           0x7C00006A, X  , General        , 0),
  INSTRUCTION(DCBST,          0x7C00006C, X  , General        , 0),
  INSTRUCTION(LWZUx,          0x7C00006E, X  , General        , 0),
  INSTRUCTION(CNTLZDx,        0x7C000074, X  , General        , 0),
  INSTRUCTION(ANDCx,          0x7C000078, X  , General        , 0),
  INSTRUCTION(TD,             0x7C000088, X  , General        , 0),
  INSTRUCTION(LVEWX,          0x7C00008E, X  , General        , 0),
  INSTRUCTION(MULHDx,         0x7C000092, XO , General        , 0),
  INSTRUCTION(MULHWx,         0x7C000096, XO , General        , 0),
  INSTRUCTION(LDARx,          0x7C0000A8, X  , General        , 0),
  INSTRUCTION(DCBF,           0x7C0000AC, X  , General        , 0),
  INSTRUCTION(LBZx,           0x7C0000AE, X  , General        , 0),
  INSTRUCTION(LVX,            0x7C0000CE, X  , General        , 0),
  INSTRUCTION(NEGx,           0x7C0000D0, XO , General        , 0),
  INSTRUCTION(LBZUx,          0x7C0000EE, X  , General        , 0),
  INSTRUCTION(NORx,           0x7C0000F8, X  , General        , 0),
  INSTRUCTION(STVEBX,         0x7C00010E, X  , General        , 0),
  INSTRUCTION(SUBFEx,         0x7C000110, XO , General        , 0),
  INSTRUCTION(ADDEx,          0x7C000114, XO , General        , 0),
  INSTRUCTION(MTCRF,          0x7C000120, XFX, General        , 0),
  INSTRUCTION(STDx,           0x7C00012A, X  , General        , 0),
  INSTRUCTION(STWCx,          0x7C00012D, X  , General        , 0),
  INSTRUCTION(STWx,           0x7C00012E, X  , General        , 0),
  INSTRUCTION(STVEHX,         0x7C00014E, X  , General        , 0),
  INSTRUCTION(STDUx,          0x7C00016A, X  , General        , 0),
  INSTRUCTION(STWUx,          0x7C00016E, X  , General        , 0),
  INSTRUCTION(STVEWX,         0x7C00018E, X  , General        , 0),
  INSTRUCTION(SUBFZEx,        0x7C000190, XO , General        , 0),
  INSTRUCTION(ADDZEx,         0x7C000194, XO , General        , 0),
  INSTRUCTION(STDCx,          0x7C0001AD, X  , General        , 0),
  INSTRUCTION(STBx,           0x7C0001AE, X  , General        , 0),
  INSTRUCTION(STVX,           0x7C0001CE, X  , General        , 0),
  INSTRUCTION(SUBFMEx,        0x7C0001D0, XO , General        , 0),
  INSTRUCTION(MULLDx,         0x7C0001D2, XO , General        , 0),
  INSTRUCTION(ADDMEx,         0x7C0001D4, XO , General        , 0),
  INSTRUCTION(MULLWx,         0x7C0001D6, XO , General        , 0),
  INSTRUCTION(DCBTST,         0x7C0001EC, X  , General        , 0),
  INSTRUCTION(STBUx,          0x7C0001EE, X  , General        , 0),
  INSTRUCTION(ADDx,           0x7C000214, XO , General        , 0),
  INSTRUCTION(DCBT,           0x7C00022C, X  , General        , 0),
  INSTRUCTION(LHZx,           0x7C00022E, X  , General        , 0),
  INSTRUCTION(EQVx,           0x7C000238, X  , General        , 0),
  INSTRUCTION(ECIWx,          0x7C00026C, X  , General        , 0),
  INSTRUCTION(LHZUx,          0x7C00026E, X  , General        , 0),
  INSTRUCTION(XORx,           0x7C000278, X  , General        , 0),
  INSTRUCTION(MFSPR,          0x7C0002A6, XFX, General        , 0),
  INSTRUCTION(LWAx,           0x7C0002AA, X  , General        , 0),
  INSTRUCTION(LHAx,           0x7C0002AE, X  , General        , 0),
  INSTRUCTION(LVXL,           0x7C0002CE, X  , General        , 0),
  INSTRUCTION(MFTB,           0x7C0002E6, XFX, General        , 0),
  INSTRUCTION(LWAUx,          0x7C0002EA, X  , General        , 0),
  INSTRUCTION(LHAUx,          0x7C0002EE, X  , General        , 0),
  INSTRUCTION(STHx,           0x7C00032E, X  , General        , 0),
  INSTRUCTION(ORCx,           0x7C000338, X  , General        , 0),
  INSTRUCTION(ECOWx,          0x7C00036C, X  , General        , 0),
  INSTRUCTION(STHUx,          0x7C00036E, X  , General        , 0),
  INSTRUCTION(ORx,            0x7C000378, X  , General        , 0),
  INSTRUCTION(DIVDUx,         0x7C000392, XO , General        , 0),
  INSTRUCTION(DIVWUx,         0x7C000396, XO , General        , 0),
  INSTRUCTION(MTSPR,          0x7C0003A6, XFX, General        , 0),
  INSTRUCTION(NANDx,          0x7C0003B8, X  , General        , 0),
  INSTRUCTION(STVXL,          0x7C0003CE, X  , General        , 0),
  INSTRUCTION(DIVDx,          0x7C0003D2, XO , General        , 0),
  INSTRUCTION(DIVWx,          0x7C0003D6, XO , General        , 0),
  INSTRUCTION(LVLX,           0x7C00040E, X  , General        , 0),
  INSTRUCTION(LDBRx,          0x7C000428, X  , General        , 0),
  INSTRUCTION(LSWx,           0x7C00042A, X  , General        , 0),
  INSTRUCTION(LWBRx,          0x7C00042C, X  , General        , 0),
  INSTRUCTION(LFSx,           0x7C00042E, X  , General        , 0),
  INSTRUCTION(SRWx,           0x7C000430, X  , General        , 0),
  INSTRUCTION(SRDx,           0x7C000436, X  , General        , 0),
  INSTRUCTION(LFSUx,          0x7C00046E, X  , General        , 0),
  INSTRUCTION(LSWI,           0x7C0004AA, X  , General        , 0),
  INSTRUCTION(SYNC,           0x7C0004AC, X  , General        , 0),
  INSTRUCTION(LFDx,           0x7C0004AE, X  , General        , 0),
  INSTRUCTION(LFDUx,          0x7C0004EE, X  , General        , 0),
  INSTRUCTION(STDBRx,         0x7C000528, X  , General        , 0),
  INSTRUCTION(STSWx,          0x7C00052A, X  , General        , 0),
  INSTRUCTION(STWBRx,         0x7C00052C, X  , General        , 0),
  INSTRUCTION(STFSx,          0x7C00052E, X  , General        , 0),
  INSTRUCTION(STFSUx,         0x7C00056E, X  , General        , 0),
  INSTRUCTION(STSWI,          0x7C0005AA, X  , General        , 0),
  INSTRUCTION(STFDx,          0x7C0005AE, X  , General        , 0),
  INSTRUCTION(STFDUx,         0x7C0005EE, X  , General        , 0),
  INSTRUCTION(LHBRx,          0x7C00062C, X  , General        , 0),
  INSTRUCTION(SRAWx,          0x7C000630, X  , General        , 0),
  INSTRUCTION(SRADx,          0x7C000634, X  , General        , 0),
  INSTRUCTION(SRAWIx,         0x7C000670, X  , General        , 0),
  INSTRUCTION(SRADIx,         0x7C000674, XS , General        , 0), // TODO
  INSTRUCTION(EIEIO,          0x7C0006AC, X  , General        , 0),
  INSTRUCTION(STHBRx,         0x7C00072C, X  , General        , 0),
  INSTRUCTION(EXTSHx,         0x7C000734, X  , General        , 0),
  INSTRUCTION(EXTSBx,         0x7C000774, X  , General        , 0),
  INSTRUCTION(ICBI,           0x7C0007AC, X  , General        , 0),
  INSTRUCTION(STFIWx,         0x7C0007AE, X  , General        , 0),
  INSTRUCTION(EXTSWx,         0x7C0007B4, X  , General        , 0),
  INSTRUCTION(DCBZ,           0x7C0007EC, X  , General        , 0), // 0x7C2007EC = DCBZ128
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_31 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_31_unprep, XECOUNT(xe_ppc_instr_table_31_unprep), 1, 10);

// Opcode = 58, index = bits 1-0 (2)
static xe_ppc_instr_type_t xe_ppc_instr_table_58_unprep[] = {
  INSTRUCTION(LD,             0xE8000000, DS , General        , 0),
  INSTRUCTION(LDU,            0xE8000001, DS , General        , 0),
  INSTRUCTION(LWA,            0xE8000002, DS , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_58 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_58_unprep, XECOUNT(xe_ppc_instr_table_58_unprep), 0, 1);

// Opcode = 59, index = bits 5-1 (5)
static xe_ppc_instr_type_t xe_ppc_instr_table_59_unprep[] = {
  INSTRUCTION(FDIVSx,         0xEC000024, A  , General        , 0),
  INSTRUCTION(FSUBSx,         0xEC000028, A  , General        , 0),
  INSTRUCTION(FADDSx,         0xEC00002A, A  , General        , 0),
  INSTRUCTION(FSQRTSx,        0xEC00002C, A  , General        , 0),
  INSTRUCTION(FRESx,          0xEC000030, A  , General        , 0),
  INSTRUCTION(FMULSx,         0xEC000032, A  , General        , 0),
  INSTRUCTION(FMSUBSx,        0xEC000038, A  , General        , 0),
  INSTRUCTION(FMADDSx,        0xEC00003A, A  , General        , 0),
  INSTRUCTION(FNMSUBSx,       0xEC00003C, A  , General        , 0),
  INSTRUCTION(FNMADDSx,       0xEC00003E, A  , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_59 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_59_unprep, XECOUNT(xe_ppc_instr_table_59_unprep), 1, 5);

// Opcode = 62, index = bits 1-0 (2)
static xe_ppc_instr_type_t xe_ppc_instr_table_62_unprep[] = {
  INSTRUCTION(STD,            0xF8000000, DS , General        , 0),
  INSTRUCTION(STDU,           0xF8000001, DS , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_62 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_62_unprep, XECOUNT(xe_ppc_instr_table_62_unprep), 0, 1);

// Opcode = 63, index = bits 10-1 (10)
static xe_ppc_instr_type_t xe_ppc_instr_table_63_unprep[] = {
  INSTRUCTION(FCMPU,          0xFC000000, X  , General        , 0),
  INSTRUCTION(FRSPx,          0xFC000018, X  , General        , 0),
  INSTRUCTION(FCTIWx,         0xFC00001C, X  , General        , 0),
  INSTRUCTION(FCTIWZx,        0xFC00001E, X  , General        , 0),
  INSTRUCTION(FDIVx,          0xFC000024, A  , General        , 0),
  INSTRUCTION(FSUBx,          0xFC000028, A  , General        , 0),
  INSTRUCTION(FADDx,          0xFC00002A, A  , General        , 0),
  INSTRUCTION(FSQRTx,         0xFC00002C, A  , General        , 0),
  INSTRUCTION(FSELx,          0xFC00002E, A  , General        , 0),
  INSTRUCTION(FMULx,          0xFC000032, A  , General        , 0),
  INSTRUCTION(FRSQRTEx,       0xFC000034, A  , General        , 0),
  INSTRUCTION(FMSUBx,         0xFC000038, A  , General        , 0),
  INSTRUCTION(FMADDx,         0xFC00003A, A  , General        , 0),
  INSTRUCTION(FNMSUBx,        0xFC00003C, A  , General        , 0),
  INSTRUCTION(FNMADDx,        0xFC00003E, A  , General        , 0),
  INSTRUCTION(FCMPO,          0xFC000040, X  , General        , 0),
  INSTRUCTION(MTFSB1x,        0xFC00004C, X  , General        , 0),
  INSTRUCTION(FNEGx,          0xFC000050, X  , General        , 0),
  INSTRUCTION(MCRFS,          0xFC000080, X  , General        , 0),
  INSTRUCTION(MTFSB0x,        0xFC00008C, X  , General        , 0),
  INSTRUCTION(FMRx,           0xFC000090, X  , General        , 0),
  INSTRUCTION(MTFSFIx,        0xFC00010C, X  , General        , 0),
  INSTRUCTION(FNABSx,         0xFC000110, X  , General        , 0),
  INSTRUCTION(FABSx,          0xFC000210, X  , General        , 0),
  INSTRUCTION(MFFSx,          0xFC00048E, X  , General        , 0),
  INSTRUCTION(MTFSFx,         0xFC00058E, XFL, General        , 0),
  INSTRUCTION(FCTIDx,         0xFC00065C, X  , General        , 0),
  INSTRUCTION(FCTIDZx,        0xFC00065E, X  , General        , 0),
  INSTRUCTION(FCFIDx,         0xFC00069C, X  , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table_63 = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_63_unprep, XECOUNT(xe_ppc_instr_table_63_unprep), 1, 10);

// Main table, index = bits 31-26 (6) : (code >> 26)
static xe_ppc_instr_type_t xe_ppc_instr_table_unprep[64] = {
  INSTRUCTION(TDI,            0x08000000, D  , General        , 0),
  INSTRUCTION(TWI,            0x0C000000, D  , General        , 0),
  INSTRUCTION(MULLI,          0x1C000000, D  , General        , 0),
  INSTRUCTION(SUBFICx,        0x20000000, D  , General        , 0),
  INSTRUCTION(CMPLI,          0x28000000, D  , General        , 0),
  INSTRUCTION(CMPI,           0x2C000000, D  , General        , 0),
  INSTRUCTION(ADDIC,          0x30000000, D  , General        , 0),
  INSTRUCTION(ADDICx,         0x34000000, D  , General        , 0),
  INSTRUCTION(ADDI,           0x38000000, D  , General        , 0),
  INSTRUCTION(ADDIS,          0x3C000000, D  , General        , 0),
  INSTRUCTION(BCx,            0x40000000, B  , BranchCond     , 0),
  INSTRUCTION(SC,             0x44000002, SC , Syscall        , 0),
  INSTRUCTION(Bx,             0x48000000, I  , BranchAlways   , 0),
  INSTRUCTION(RLWIMIx,        0x50000000, M  , General        , 0),
  INSTRUCTION(RLWINMx,        0x54000000, M  , General        , 0),
  INSTRUCTION(RLWNMx,         0x5C000000, M  , General        , 0),
  INSTRUCTION(ORI,            0x60000000, D  , General        , 0),
  INSTRUCTION(ORIS,           0x64000000, D  , General        , 0),
  INSTRUCTION(XORI,           0x68000000, D  , General        , 0),
  INSTRUCTION(XORIS,          0x6C000000, D  , General        , 0),
  INSTRUCTION(ANDIx,          0x70000000, D  , General        , 0),
  INSTRUCTION(ANDISx,         0x74000000, D  , General        , 0),
  INSTRUCTION(LWZ,            0x80000000, D  , General        , 0),
  INSTRUCTION(LWZU,           0x84000000, D  , General        , 0),
  INSTRUCTION(LBZ,            0x88000000, D  , General        , 0),
  INSTRUCTION(LBZU,           0x8C000000, D  , General        , 0),
  INSTRUCTION(STW,            0x90000000, D  , General        , 0),
  INSTRUCTION(STWU,           0x94000000, D  , General        , 0),
  INSTRUCTION(STB,            0x98000000, D  , General        , 0),
  INSTRUCTION(STBU,           0x9C000000, D  , General        , 0),
  INSTRUCTION(LHZ,            0xA0000000, D  , General        , 0),
  INSTRUCTION(LHZU,           0xA4000000, D  , General        , 0),
  INSTRUCTION(LHA,            0xA8000000, D  , General        , 0),
  INSTRUCTION(LHAU,           0xAC000000, D  , General        , 0),
  INSTRUCTION(STH,            0xB0000000, D  , General        , 0),
  INSTRUCTION(STHU,           0xB4000000, D  , General        , 0),
  INSTRUCTION(LMW,            0xB8000000, D  , General        , 0),
  INSTRUCTION(STMW,           0xBC000000, D  , General        , 0),
  INSTRUCTION(LFS,            0xC0000000, D  , General        , 0),
  INSTRUCTION(LFSU,           0xC4000000, D  , General        , 0),
  INSTRUCTION(LFD,            0xC8000000, D  , General        , 0),
  INSTRUCTION(LFDU,           0xCC000000, D  , General        , 0),
  INSTRUCTION(STFS,           0xD0000000, D  , General        , 0),
  INSTRUCTION(STFSU,          0xD4000000, D  , General        , 0),
  INSTRUCTION(STFD,           0xD8000000, D  , General        , 0),
  INSTRUCTION(STFDU,          0xDC000000, D  , General        , 0),
};
static xe_ppc_instr_type_t *xe_ppc_instr_table = xe_ppc_instr_table_prep(
    xe_ppc_instr_table_unprep, XECOUNT(xe_ppc_instr_table_unprep), 26, 31);


#undef FLAG
#undef INSTRUCTION
#undef EMPTY

#endif  // XENIA_CPU_PPC_INSTR_TABLE_H_
