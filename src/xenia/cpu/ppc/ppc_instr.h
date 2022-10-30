/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_INSTR_H_
#define XENIA_CPU_PPC_PPC_INSTR_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xe {
namespace cpu {
namespace ppc {

struct PPCOpcodeBits {
  union {
    uint32_t code;

    // kXEPPCInstrFormatI
    struct {
      uint32_t LK : 1;
      uint32_t AA : 1;
      uint32_t LI : 24;
      uint32_t : 6;
    } I;
    // kXEPPCInstrFormatB
    struct {
      uint32_t LK : 1;
      uint32_t AA : 1;
      uint32_t BD : 14;
      uint32_t BI : 5;
      uint32_t BO : 5;
      uint32_t : 6;
    } B;

    // kXEPPCInstrFormatSC
    struct {
      uint32_t : 5;
      uint32_t LEV : 7;
      uint32_t : 20;
    } SC;
    // kXEPPCInstrFormatD
    struct {
      uint32_t DS : 16;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } D;
    // kXEPPCInstrFormatDS
    struct {
      uint32_t : 2;
      uint32_t DS : 14;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } DS;
    // kXEPPCInstrFormatX
    struct {
      uint32_t Rc : 1;
      uint32_t : 10;
      uint32_t RB : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } X;
    // kXEPPCInstrFormatXL
    struct {
      uint32_t LK : 1;
      uint32_t : 10;
      uint32_t BB : 5;
      uint32_t BI : 5;
      uint32_t BO : 5;
      uint32_t : 6;
    } XL;
    // kXEPPCInstrFormatXFX
    struct {
      uint32_t : 1;
      uint32_t : 10;
      uint32_t spr : 10;
      uint32_t RT : 5;
      uint32_t : 6;
    } XFX;
    // kXEPPCInstrFormatXFL
    struct {
      uint32_t Rc : 1;
      uint32_t : 10;
      uint32_t RB : 5;
      uint32_t W : 1;
      uint32_t FM : 8;
      uint32_t L : 1;
      uint32_t : 6;
    } XFL;
    // kXEPPCInstrFormatXS
    struct {
      uint32_t Rc : 1;
      uint32_t SH5 : 1;
      uint32_t : 9;
      uint32_t SH : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } XS;
    // kXEPPCInstrFormatXO
    struct {
      uint32_t Rc : 1;
      uint32_t : 9;
      uint32_t OE : 1;
      uint32_t RB : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } XO;
    // kXEPPCInstrFormatA
    struct {
      uint32_t Rc : 1;
      uint32_t XO : 5;
      uint32_t FRC : 5;
      uint32_t FRB : 5;
      uint32_t FRA : 5;
      uint32_t FRT : 5;
      uint32_t : 6;
    } A;
    // kXEPPCInstrFormatM
    struct {
      uint32_t Rc : 1;
      uint32_t ME : 5;
      uint32_t MB : 5;
      uint32_t SH : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } M;
    // kXEPPCInstrFormatMD
    struct {
      uint32_t Rc : 1;
      uint32_t SH5 : 1;
      uint32_t idx : 3;
      uint32_t MB5 : 1;
      uint32_t MB : 5;
      uint32_t SH : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } MD;
    // kXEPPCInstrFormatMDS
    struct {
      uint32_t Rc : 1;
      uint32_t idx : 4;
      uint32_t MB5 : 1;
      uint32_t MB : 5;
      uint32_t RB : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } MDS;
    // kXEPPCInstrFormatMDSH
    struct {
      uint32_t Rc : 1;
      uint32_t idx : 4;
      uint32_t MB5 : 1;
      uint32_t MB : 5;
      uint32_t RB : 5;
      uint32_t RA : 5;
      uint32_t RT : 5;
      uint32_t : 6;
    } MDSH;
    // kXEPPCInstrFormatVXA
    struct {
      uint32_t : 6;
      uint32_t VC : 5;
      uint32_t VB : 5;
      uint32_t VA : 5;
      uint32_t VD : 5;
      uint32_t : 6;
    } VXA;
    // kXEPPCInstrFormatVX
    struct {
      uint32_t : 11;
      uint32_t VB : 5;
      uint32_t VA : 5;
      uint32_t VD : 5;
      uint32_t : 6;
    } VX;
    // kXEPPCInstrFormatVXR
    struct {
      uint32_t : 10;
      uint32_t Rc : 1;
      uint32_t VB : 5;
      uint32_t VA : 5;
      uint32_t VD : 5;
      uint32_t : 6;
    } VXR;
    // kXEPPCInstrFormatVX128
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 1;
      uint32_t VA128h : 1;
      uint32_t : 4;
      uint32_t VA128H : 1;
      uint32_t VB128l : 5;
      uint32_t VA128l : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128;
    // kXEPPCInstrFormatVX128_1
    struct {
      // VD128 = VD128l | (VD128h << 5)
      uint32_t : 2;
      uint32_t VD128h : 2;
      uint32_t : 7;
      uint32_t RB : 5;
      uint32_t RA : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_1;
    // kXEPPCInstrFormatVX128_2
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 1;
      uint32_t VA128h : 1;
      uint32_t VC : 3;
      uint32_t : 1;
      uint32_t VA128H : 1;
      uint32_t VB128l : 5;
      uint32_t VA128l : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_2;
    // kXEPPCInstrFormatVX128_3
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 7;
      uint32_t VB128l : 5;
      uint32_t IMM : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_3;
    // kXEPPCInstrFormatVX128_4
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 2;
      uint32_t z : 2;
      uint32_t : 3;
      uint32_t VB128l : 5;
      uint32_t IMM : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_4;
    // kXEPPCInstrFormatVX128_5
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 1;
      uint32_t VA128h : 1;
      uint32_t SH : 4;
      uint32_t VA128H : 1;
      uint32_t VB128l : 5;
      uint32_t VA128l : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_5;
    // kXEPPCInstrFormatVX128_P
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VB128 = VB128l | (VB128h << 5)
      // PERM = PERMl | (PERMh << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 2;
      uint32_t PERMh : 3;
      uint32_t : 2;
      uint32_t VB128l : 5;
      uint32_t PERMl : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_P;
    // kXEPPCInstrFormatVX128_R
    struct {
      // VD128 = VD128l | (VD128h << 5)
      // VA128 = VA128l | (VA128h << 5) | (VA128H << 6)
      // VB128 = VB128l | (VB128h << 5)
      uint32_t VB128h : 2;
      uint32_t VD128h : 2;
      uint32_t : 1;
      uint32_t VA128h : 1;
      uint32_t Rc : 1;
      uint32_t : 3;
      uint32_t VA128H : 1;
      uint32_t VB128l : 5;
      uint32_t VA128l : 5;
      uint32_t VD128l : 5;
      uint32_t : 6;
    } VX128_R;
    // kXEPPCInstrFormatXDSS
    struct {
    } XDSS;
  };
};

// DEPRECATED
// TODO(benvanik): move code to PPCDecodeData.
struct InstrData : public PPCOpcodeBits {
  PPCOpcode opcode;
  const PPCOpcodeInfo* opcode_info;
  uint32_t address;
};

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_INSTR_H_
