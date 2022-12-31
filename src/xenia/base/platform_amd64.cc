/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#define XBYAK_NO_OP_NAMES
#include "third_party/xbyak/xbyak/xbyak.h"
#include "third_party/xbyak/xbyak/xbyak_util.h"
DEFINE_int64(x64_extension_mask, -1LL,
             "Allow the detection and utilization of specific instruction set "
             "features.\n"
             "    0 = x86_64 + AVX1\n"
             "    1 = AVX2\n"
             "    2 = FMA\n"
             "    4 = LZCNT\n"
             "    8 = BMI1\n"
             "   16 = BMI2\n"
             "   32 = F16C\n"
             "   64 = Movbe\n"
             "  128 = GFNI\n"
             "  256 = AVX512F\n"
             "  512 = AVX512VL\n"
             " 1024 = AVX512BW\n"
             " 2048 = AVX512DQ\n"
             " 4096 = AVX512VBMI\n"
             "   -1 = Detect and utilize all possible processor features\n",
             "x64");
namespace xe {
namespace amd64 {
static uint64_t g_feature_flags = 0U;
static bool g_did_initialize_feature_flags = false;
uint64_t GetFeatureFlags() {
  xenia_assert(g_did_initialize_feature_flags);
  return g_feature_flags;
}
XE_COLD
XE_NOINLINE
void InitFeatureFlags() {
  uint64_t feature_flags_ = 0U;
  {
    Xbyak::util::Cpu cpu_;
#define TEST_EMIT_FEATURE(emit, ext)                \
  if ((cvars::x64_extension_mask & emit) == emit) { \
    feature_flags_ |= (cpu_.has(ext) ? emit : 0);   \
  }

    TEST_EMIT_FEATURE(kX64EmitAVX2, Xbyak::util::Cpu::tAVX2);
    TEST_EMIT_FEATURE(kX64EmitFMA, Xbyak::util::Cpu::tFMA);
    TEST_EMIT_FEATURE(kX64EmitLZCNT, Xbyak::util::Cpu::tLZCNT);
    TEST_EMIT_FEATURE(kX64EmitBMI1, Xbyak::util::Cpu::tBMI1);
    TEST_EMIT_FEATURE(kX64EmitBMI2, Xbyak::util::Cpu::tBMI2);
    TEST_EMIT_FEATURE(kX64EmitMovbe, Xbyak::util::Cpu::tMOVBE);
    TEST_EMIT_FEATURE(kX64EmitGFNI, Xbyak::util::Cpu::tGFNI);
    TEST_EMIT_FEATURE(kX64EmitAVX512F, Xbyak::util::Cpu::tAVX512F);
    TEST_EMIT_FEATURE(kX64EmitAVX512VL, Xbyak::util::Cpu::tAVX512VL);
    TEST_EMIT_FEATURE(kX64EmitAVX512BW, Xbyak::util::Cpu::tAVX512BW);
    TEST_EMIT_FEATURE(kX64EmitAVX512DQ, Xbyak::util::Cpu::tAVX512DQ);
    TEST_EMIT_FEATURE(kX64EmitAVX512VBMI, Xbyak::util::Cpu::tAVX512VBMI);
    TEST_EMIT_FEATURE(kX64EmitPrefetchW, Xbyak::util::Cpu::tPREFETCHW);
#undef TEST_EMIT_FEATURE
    /*
    fix for xbyak bug/omission, amd cpus are never checked for lzcnt. fixed in
    latest version of xbyak
  */
    unsigned int data[4];
    Xbyak::util::Cpu::getCpuid(0x80000001, data);
    unsigned amd_flags = data[2];
    if (amd_flags & (1U << 5)) {
      if ((cvars::x64_extension_mask & kX64EmitLZCNT) == kX64EmitLZCNT) {
        feature_flags_ |= kX64EmitLZCNT;
      }
    }
    // todo: although not reported by cpuid, zen 1 and zen+ also have fma4
    if (amd_flags & (1U << 16)) {
      if ((cvars::x64_extension_mask & kX64EmitFMA4) == kX64EmitFMA4) {
        feature_flags_ |= kX64EmitFMA4;
      }
    }
    if (amd_flags & (1U << 21)) {
      if ((cvars::x64_extension_mask & kX64EmitTBM) == kX64EmitTBM) {
        feature_flags_ |= kX64EmitTBM;
      }
    }
    if (amd_flags & (1U << 11)) {
      if ((cvars::x64_extension_mask & kX64EmitXOP) == kX64EmitXOP) {
        feature_flags_ |= kX64EmitXOP;
      }
    }
    if (cpu_.has(Xbyak::util::Cpu::tAMD)) {
      bool is_zennish = cpu_.displayFamily >= 0x17;
      /*
                  chrispy: according to agner's tables, all amd architectures
         that we support (ones with avx) have the same timings for
         jrcxz/loop/loope/loopne as for other jmps
          */
      feature_flags_ |= kX64FastJrcx;
      feature_flags_ |= kX64FastLoop;
      if (is_zennish) {
        // ik that i heard somewhere that this is the case for zen, but i need
        // to verify. cant find my original source for that. todo: ask agner?
        feature_flags_ |= kX64FlagsIndependentVars;
      }
    }
  }
  {
    unsigned int data[4];
    memset(data, 0, sizeof(data));
    // intel extended features
    Xbyak::util::Cpu::getCpuidEx(7, 0, data);
    if ((data[2] & (1 << 28)) &&
        (cvars::x64_extension_mask & kX64EmitMovdir64M)) {
      feature_flags_ |= kX64EmitMovdir64M;
    }
    if ((data[1] & (1 << 9)) && (cvars::x64_extension_mask & kX64FastRepMovs)) {
      feature_flags_ |= kX64FastRepMovs;
    }
  }
  g_feature_flags = feature_flags_;
  g_did_initialize_feature_flags = true;
}
}  // namespace amd64
}  // namespace xe