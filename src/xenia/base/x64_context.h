/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_X64_CONTEXT_H_
#define XENIA_BASE_X64_CONTEXT_H_

#include <xmmintrin.h>

#include <cstdint>
#include <string>

namespace xe {

enum class X64Register {
  // NOTE: this order matches 1:1 with the order in the X64Context.
  // NOTE: this order matches 1:1 with a string table in the x64_context.cc.
  kRip,
  kEflags,
  kRax,
  kRcx,
  kRdx,
  kRbx,
  kRsp,
  kRbp,
  kRsi,
  kRdi,
  kR8,
  kR9,
  kR10,
  kR11,
  kR12,
  kR13,
  kR14,
  kR15,
  kXmm0,
  kXmm1,
  kXmm2,
  kXmm3,
  kXmm4,
  kXmm5,
  kXmm6,
  kXmm7,
  kXmm8,
  kXmm9,
  kXmm10,
  kXmm11,
  kXmm12,
  kXmm13,
  kXmm14,
  kXmm15,
};

class X64Context {
 public:
  uint64_t rip;
  uint32_t eflags;
  union {
    struct {
      uint64_t rax;
      uint64_t rcx;
      uint64_t rdx;
      uint64_t rbx;
      uint64_t rsp;
      uint64_t rbp;
      uint64_t rsi;
      uint64_t rdi;
      uint64_t r8;
      uint64_t r9;
      uint64_t r10;
      uint64_t r11;
      uint64_t r12;
      uint64_t r13;
      uint64_t r14;
      uint64_t r15;
    };
    uint64_t int_registers[16];
  };

  union {
    struct {
      __m128 xmm0;
      __m128 xmm1;
      __m128 xmm2;
      __m128 xmm3;
      __m128 xmm4;
      __m128 xmm5;
      __m128 xmm6;
      __m128 xmm7;
      __m128 xmm8;
      __m128 xmm9;
      __m128 xmm10;
      __m128 xmm11;
      __m128 xmm12;
      __m128 xmm13;
      __m128 xmm14;
      __m128 xmm15;
    };
    __m128 xmm_registers[16];
  };

  static const char* GetRegisterName(X64Register reg);
  std::string GetStringFromValue(X64Register reg, bool hex) const;
  void SetValueFromString(X64Register reg, std::string value, bool hex);
};

}  // namespace xe

#endif  // XENIA_BASE_X64_CONTEXT_H_
