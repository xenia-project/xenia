/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_HOST_THREAD_CONTEXT_H_
#define XENIA_BASE_HOST_THREAD_CONTEXT_H_

#include <cstdint>
#include <string>

#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

#if XE_ARCH_AMD64
#include <xmmintrin.h>
#endif

namespace xe {

// NOTE: The order of the registers in the enumerations must match the order in
// the string table in host_thread_context.cc, as well as remapping tables in
// exception handler implementations.

enum class X64Register {
  kRip,
  kEflags,

  kIntRegisterFirst,
  // The order matches the indices in the instruction encoding, as well as the
  // Windows CONTEXT structure.
  kRax = kIntRegisterFirst,
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
  kIntRegisterLast = kR15,

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

enum class Arm64Register {
  kX0,
  kX1,
  kX2,
  kX3,
  kX4,
  kX5,
  kX6,
  kX7,
  kX8,
  kX9,
  kX10,
  kX11,
  kX12,
  kX13,
  kX14,
  kX15,
  kX16,
  kX17,
  kX18,
  kX19,
  kX20,
  kX21,
  kX22,
  kX23,
  kX24,
  kX25,
  kX26,
  kX27,
  kX28,
  // FP (frame pointer).
  kX29,
  // LR (link register).
  kX30,
  kSp,
  kPc,
  kPstate,
  kFpsr,
  kFpcr,
  // The whole 128 bits of a Vn register are also known as Qn (quadword).
  kV0,
  kV1,
  kV2,
  kV3,
  kV4,
  kV5,
  kV6,
  kV7,
  kV8,
  kV9,
  kV10,
  kV11,
  kV12,
  kV13,
  kV14,
  kV15,
  kV16,
  kV17,
  kV18,
  kV19,
  kV20,
  kV21,
  kV22,
  kV23,
  kV24,
  kV25,
  kV26,
  kV27,
  kV28,
  kV29,
  kV30,
  kV31,
};

#if XE_ARCH_AMD64
using HostRegister = X64Register;
#elif XE_ARCH_ARM64
using HostRegister = Arm64Register;
#else
enum class HostRegister {};
#endif  // XE_ARCH

class HostThreadContext {
 public:
#if XE_ARCH_AMD64
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
#elif XE_ARCH_ARM64
  uint64_t x[31];
  uint64_t sp;
  uint64_t pc;
  uint64_t pstate;
  uint32_t cpsr;
  uint32_t fpsr;
  uint32_t fpcr;
  vec128_t v[32];
#endif  // XE_ARCH

  static const char* GetRegisterName(HostRegister reg);
  std::string GetStringFromValue(HostRegister reg, bool hex) const;
};

}  // namespace xe

#endif  // XENIA_BASE_X64_CONTEXT_H_
