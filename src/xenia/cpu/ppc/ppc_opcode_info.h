/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_OPCODE_INFO_H_
#define XENIA_CPU_PPC_PPC_OPCODE_INFO_H_

#include <cstdint>

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/ppc/ppc_opcode.h"

namespace xe {
namespace cpu {
namespace ppc {

struct InstrData;
class PPCHIRBuilder;

enum class PPCOpcodeFormat {
  kSC,
  kD,
  kDS,
  kB,
  kI,
  kX,
  kXL,
  kXFX,
  kXFL,
  kXS,
  kXO,
  kA,
  kM,
  kMD,
  kMDS,
  kDCBZ,
  kVX,
  kVC,
  kVA,
  kVX128,
  kVX128_1,
  kVX128_2,
  kVX128_3,
  kVX128_4,
  kVX128_5,
  kVX128_R,
  kVX128_P,
};

enum class PPCOpcodeGroup {
  kB,  // Branching/traps/etc.
  kC,  // Control.
  kM,  // Memory load/store (of all types).
  kI,  // Integer.
  kF,  // Floating-point.
  kV,  // VMX.
};

enum class PPCOpcodeType {
  kGeneral,
  kSync,
};

typedef int (*InstrEmitFn)(PPCHIRBuilder& f, const InstrData& i);

struct PPCOpcodeInfo {
  PPCOpcodeGroup group;
  PPCOpcodeType type;
  InstrEmitFn emit;
};

struct PPCDecodeData;
typedef void (*InstrDisasmFn)(const PPCDecodeData& d, StringBuffer* str);

enum class PPCOpcodeField : uint32_t {
  kRA,
  kRA0,  // 0 if RA==0 else RA
  kRB,
  kRD,
  kRS,  // alias for RD
  kOE,
  kOEcond,  // iff OE=1
  kCR,
  kCRcond,  // iff Rc=1
  kCA,
  kCRM,
  kIMM,
  kSIMM,
  kUIMM,
  kd,  // displacement
  kds,
  kLR,
  kLRcond,
  kADDR,
  kBI,
  kBO,
  kCTR,
  kCTRcond,
  kL,
  kLK,
  kAA,
  kCRFD,
  kCRFS,
  kCRBA,
  kCRBB,
  kCRBD,
  kFPSCR,
  kFPSCRD,
  kMSR,
  kSPR,
  kVSCR,
  kTBR,
  kFM,
  kFA,
  kFB,
  kFC,
  kFD,
  kFS,
  kVA,
  kVB,
  kVC,
  kVD,
  kVS,
  kSH,
  kSHB,
  kME,
  kMB,
  kTO,
  kLEV,
};
#pragma pack(push, 1)
struct PPCOpcodeDisasmInfo {
  PPCOpcodeGroup group;
  PPCOpcodeFormat format;
  uint32_t opcode;
  const char* name;
  const char* description;
  // std::vector<PPCOpcodeField> reads;
  // std::vector<PPCOpcodeField> writes;
  InstrDisasmFn disasm;
};
#pragma pack(pop)
PPCOpcode LookupOpcode(uint32_t code);

const PPCOpcodeInfo& GetOpcodeInfo(PPCOpcode opcode);
const PPCOpcodeDisasmInfo& GetOpcodeDisasmInfo(PPCOpcode opcode);

void RegisterOpcodeEmitter(PPCOpcode opcode, InstrEmitFn fn);
void RegisterOpcodeDisasm(PPCOpcode opcode, InstrDisasmFn fn);

inline const PPCOpcodeInfo& LookupOpcodeInfo(uint32_t code) {
  return GetOpcodeInfo(LookupOpcode(code));
}

bool DisasmPPC(uint32_t address, uint32_t code, StringBuffer* str);

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_OPCODE_INFO_H_
