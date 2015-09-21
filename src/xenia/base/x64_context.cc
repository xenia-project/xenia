/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/x64_context.h"

#include "xenia/base/assert.h"
#include "xenia/base/string_util.h"

namespace xe {

// NOTE: this order matches 1:1 with the X64Register enum.
static const char* kRegisterNames[] = {
    "rip",   "eflags", "rax",   "rcx",   "rdx",   "rbx",   "rsp",
    "rbp",   "rsi",    "rdi",   "r8",    "r9",    "r10",   "r11",
    "r12",   "r13",    "r14",   "r15",   "xmm0",  "xmm1",  "xmm2",
    "xmm3",  "xmm4",   "xmm5",  "xmm6",  "xmm7",  "xmm8",  "xmm9",
    "xmm10", "xmm11",  "xmm12", "xmm13", "xmm14", "xmm15",
};

const char* X64Context::GetRegisterName(X64Register reg) {
  return kRegisterNames[static_cast<int>(reg)];
}

std::string X64Context::GetStringFromValue(X64Register reg, bool hex) const {
  switch (reg) {
    case X64Register::kRip:
      return hex ? string_util::to_hex_string(rip) : std::to_string(rip);
    case X64Register::kEflags:
      return hex ? string_util::to_hex_string(eflags) : std::to_string(eflags);
    default:
      if (static_cast<int>(reg) >= static_cast<int>(X64Register::kRax) &&
          static_cast<int>(reg) <= static_cast<int>(X64Register::kR15)) {
        auto value = int_registers[static_cast<int>(reg) -
                                   static_cast<int>(X64Register::kRax)];
        return hex ? string_util::to_hex_string(value) : std::to_string(value);
      } else if (static_cast<int>(reg) >=
                     static_cast<int>(X64Register::kXmm0) &&
                 static_cast<int>(reg) <=
                     static_cast<int>(X64Register::kXmm15)) {
        auto value = xmm_registers[static_cast<int>(reg) -
                                   static_cast<int>(X64Register::kXmm0)];
        return hex ? string_util::to_hex_string(value)
                   : string_util::to_string(value);
      } else {
        assert_unhandled_case(reg);
        return "";
      }
  }
}

void X64Context::SetValueFromString(X64Register reg, std::string value,
                                    bool hex) {
  // TODO(benvanik): set value from string.
  assert_always(false);
}

}  // namespace xe
