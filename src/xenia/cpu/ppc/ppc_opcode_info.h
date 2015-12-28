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
  kB,
  kI,
  kX,
  kXL,
  kXFX,
  kXFL,
  kVX,
  kVX128,
  kVX128_1,
  kVX128_2,
  kVX128_3,
  kVX128_4,
  kVX128_5,
  kVX128_R,
  kVX128_P,
  kVC,
  kVA,
  kXO,
  kXW,
  kA,
  kDS,
  kM,
  kMD,
  kMDS,
  kMDSH,
  kXS,
  kDCBZ,
};

enum class PPCOpcodeGroup {
  kI,
  kF,
  kV,
};

enum class PPCOpcodeType {
  kGeneral,
  kSync,
};

typedef void (*InstrDisasmFn1)(const InstrData* i, StringBuffer* str);
typedef int (*InstrEmitFn)(PPCHIRBuilder& f, const InstrData& i);

struct PPCOpcodeInfo {
  uint32_t opcode;
  const char* name;
  PPCOpcodeFormat format;
  PPCOpcodeGroup group;
  PPCOpcodeType type;
  const char* description;

  InstrDisasmFn1 disasm;
  InstrEmitFn emit;
};

PPCOpcode LookupOpcode(uint32_t code);

const PPCOpcodeInfo& GetOpcodeInfo(PPCOpcode opcode);

void RegisterOpcodeDisasm(PPCOpcode opcode, InstrDisasmFn1 fn);
void RegisterOpcodeEmitter(PPCOpcode opcode, InstrEmitFn fn);

inline const PPCOpcodeInfo& LookupOpcodeInfo(uint32_t code) {
  return GetOpcodeInfo(LookupOpcode(code));
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_OPCODE_INFO_H_
