/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"
#include "xenia/base/platform.h"

#if XE_ARCH_ARM64 && XE_CLOCK_RAW_AVAILABLE

#include "xenia/base/logging.h"

#ifdef _MSC_VER
#include <arm64_neon.h>
#include <intrin.h>
#else
#include <arm_neon.h>
#endif

// Wrap all these different cpu compiler intrinsics.
#if XE_COMPILER_MSVC
constexpr int32_t CNTFRQ_EL0 = ARM64_SYSREG(3, 3, 14, 0, 0);
constexpr int32_t CNTVCT_EL0 = ARM64_SYSREG(3, 3, 14, 0, 2);
#define xe_cpu_mrs(reg) _ReadStatusReg(reg)
#elif XE_COMPILER_CLANG || XE_COMPILER_GNUC
constexpr int32_t CNTFRQ_EL0 = 0b11'011'1110'0000'000;
constexpr int32_t CNTVCT_EL0 = 0b11'011'1110'0000'010;

uint64_t xe_cpu_mrs(uint32_t reg) {
  uint64_t result;
  __asm__ volatile("mrs \t%0," #reg : "=r"(result));
  return result;
}
#else
#error \
    "No cpu instruction wrappers xe_cpu_mrs(CNTVCT_EL0); for current compiler implemented."
#endif

namespace xe {

uint64_t Clock::host_tick_frequency_raw() { return xe_cpu_mrs(CNTFRQ_EL0); }
uint64_t Clock::host_tick_count_raw() { return xe_cpu_mrs(CNTVCT_EL0); }

}  // namespace xe

#endif
