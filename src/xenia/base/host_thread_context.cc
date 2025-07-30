/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/host_thread_context.h"

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/base/string_util.h"

namespace xe {

// NOTE: this order matches 1:1 with the HostRegister enums.
static const char* kRegisterNames[] = {
#if XE_ARCH_AMD64
    "rip",   "eflags", "rax",   "rcx",   "rdx",   "rbx",   "rsp",
    "rbp",   "rsi",    "rdi",   "r8",    "r9",    "r10",   "r11",
    "r12",   "r13",    "r14",   "r15",   "xmm0",  "xmm1",  "xmm2",
    "xmm3",  "xmm4",   "xmm5",  "xmm6",  "xmm7",  "xmm8",  "xmm9",
    "xmm10", "xmm11",  "xmm12", "xmm13", "xmm14", "xmm15",
#elif XE_ARCH_ARM64
    "x0",  "x1",  "x2",  "x3",     "x4",   "x5",   "x6",  "x7",  "x8",  "x9",
    "x10", "x11", "x12", "x13",    "x14",  "x15",  "x16", "x17", "x18", "x19",
    "x20", "x21", "x22", "x23",    "x24",  "x25",  "x26", "x27", "x28", "x29",
    "x30", "sp",  "pc",  "pstate", "fpsr", "fpcr", "v0",  "v1",  "v2",  "v3",
    "v4",  "v5",  "v6",  "v7",     "v8",   "v9",   "v10", "v11", "v12", "v13",
    "v14", "v15", "v16", "v17",    "v18",  "v19",  "v20", "v21", "v22", "v23",
    "v24", "v25", "v26", "v27",    "v28",  "v29",  "v30", "v31",
#endif  // XE_ARCH
};

const char* HostThreadContext::GetRegisterName(HostRegister reg) {
  return kRegisterNames[int(reg)];
}

std::string HostThreadContext::GetStringFromValue(HostRegister reg,
                                                  bool hex) const {
#if XE_ARCH_AMD64
  switch (reg) {
    case X64Register::kRip:
      return hex ? string_util::to_hex_string(rip) : std::to_string(rip);
    case X64Register::kEflags:
      return hex ? string_util::to_hex_string(eflags) : std::to_string(eflags);
    default:
      if (reg >= X64Register::kIntRegisterFirst &&
          reg <= X64Register::kIntRegisterLast) {
        auto value =
            int_registers[int(reg) - int(X64Register::kIntRegisterFirst)];
        return hex ? string_util::to_hex_string(value) : std::to_string(value);
      } else if (reg >= X64Register::kXmm0 && reg <= X64Register::kXmm15) {
        auto value = xmm_registers[int(reg) - int(X64Register::kXmm0)];
        return hex ? string_util::to_hex_string(value) : xe::to_string(value);
      } else {
        assert_unhandled_case(reg);
        return std::string();
      }
  }
#elif XE_ARCH_ARM64
  switch (reg) {
    case Arm64Register::kSp:
      return hex ? string_util::to_hex_string(sp) : std::to_string(sp);
    case Arm64Register::kPc:
      return hex ? string_util::to_hex_string(pc) : std::to_string(pc);
    case Arm64Register::kPstate:
      return hex ? string_util::to_hex_string(cpsr) : std::to_string(cpsr);
    case Arm64Register::kFpsr:
      return hex ? string_util::to_hex_string(fpsr) : std::to_string(fpsr);
    case Arm64Register::kFpcr:
      return hex ? string_util::to_hex_string(fpcr) : std::to_string(fpcr);
    default:
      if (reg >= Arm64Register::kX0 && reg <= Arm64Register::kX30) {
        auto value = x[int(reg) - int(Arm64Register::kX0)];
        return hex ? string_util::to_hex_string(value) : std::to_string(value);
      } else if (reg >= Arm64Register::kV0 && reg <= Arm64Register::kV31) {
        auto value = v[int(reg) - int(Arm64Register::kV0)];
        return hex ? string_util::to_hex_string(value) : xe::to_string(value);
      } else {
        assert_unhandled_case(reg);
        return std::string();
      }
  }
#else
  assert_always(
      "HostThreadContext::GetStringFromValue not implemented for the target "
      "CPU architecture");
  return std::string();
#endif  // XE_ARCH
}

}  // namespace xe
