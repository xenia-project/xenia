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

#include <cstdint>
#include <string>

#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

#if XE_ARCH_AMD64
#include <xmmintrin.h>
#endif

namespace xe {

class X64Context;

#if XE_ARCH_AMD64
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
      vec128_t xmm0;
      vec128_t xmm1;
      vec128_t xmm2;
      vec128_t xmm3;
      vec128_t xmm4;
      vec128_t xmm5;
      vec128_t xmm6;
      vec128_t xmm7;
      vec128_t xmm8;
      vec128_t xmm9;
      vec128_t xmm10;
      vec128_t xmm11;
      vec128_t xmm12;
      vec128_t xmm13;
      vec128_t xmm14;
      vec128_t xmm15;
    };
    vec128_t xmm_registers[16];
  };

  static const char* GetRegisterName(X64Register reg);
  std::string GetStringFromValue(X64Register reg, bool hex) const;
  void SetValueFromString(X64Register reg, std::string value, bool hex);
};
#endif  // XE_ARCH_AMD64

}  // namespace xe

#endif  // XENIA_BASE_X64_CONTEXT_H_
