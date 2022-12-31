/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_AMD64_H_
#define XENIA_BASE_PLATFORM_AMD64_H_
#include <cstdint>

namespace xe {
namespace amd64 {
enum X64FeatureFlags : uint64_t {
  kX64EmitAVX2 = 1 << 0,
  kX64EmitFMA = 1 << 1,
  kX64EmitLZCNT = 1 << 2,  // this is actually ABM and includes popcount
  kX64EmitBMI1 = 1 << 3,
  kX64EmitBMI2 = 1 << 4,
  kX64EmitPrefetchW = 1 << 5,
  kX64EmitMovbe = 1 << 6,
  kX64EmitGFNI = 1 << 7,

  kX64EmitAVX512F = 1 << 8,
  kX64EmitAVX512VL = 1 << 9,

  kX64EmitAVX512BW = 1 << 10,
  kX64EmitAVX512DQ = 1 << 11,
  kX64EmitAVX512VBMI = 1 << 12,

  kX64EmitAVX512Ortho = kX64EmitAVX512F | kX64EmitAVX512VL,
  kX64EmitAVX512Ortho64 = kX64EmitAVX512Ortho | kX64EmitAVX512DQ,
  kX64FastJrcx = 1 << 13,  // jrcxz is as fast as any other jump ( >= Zen1)
  kX64FastLoop =
      1 << 14,  // loop/loope/loopne is as fast as any other jump ( >= Zen2)

  kX64FlagsIndependentVars =
      1 << 15,  // if true, instructions that only modify some flags (like
                // inc/dec) do not introduce false dependencies on EFLAGS
                // because the individual flags are treated as different vars by
                // the processor. (this applies to zen)
  kX64EmitXOP = 1 << 16,   // chrispy: xop maps really well to many vmx
                           // instructions, and FX users need the boost
  kX64EmitFMA4 = 1 << 17,  // todo: also use on zen1?
  kX64EmitTBM = 1 << 18,
  kX64EmitMovdir64M = 1 << 19,
  kX64FastRepMovs = 1 << 20

};

XE_NOALIAS
uint64_t GetFeatureFlags();
XE_COLD
void InitFeatureFlags();

}  // namespace amd64
}  // namespace xe

#endif  // XENIA_BASE_PLATFORM_AMD64_H_
