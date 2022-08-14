/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/clock.h"
#include "xenia/base/platform.h"

#if XE_ARCH_AMD64 && XE_CLOCK_RAW_AVAILABLE

#include "xenia/base/logging.h"

// Wrap all these different cpu compiler intrinsics.
// So no inline assembler here and the compiler will remove the clutter.
#if XE_COMPILER_MSVC
#define xe_cpu_cpuid(level, eax, ebx, ecx, edx)              \
  {                                                          \
    int __xe_cpuid_registers_[4];                            \
    __cpuid(__xe_cpuid_registers_, (level));                 \
    (eax) = static_cast<uint32_t>(__xe_cpuid_registers_[0]); \
    (ebx) = static_cast<uint32_t>(__xe_cpuid_registers_[1]); \
    (ecx) = static_cast<uint32_t>(__xe_cpuid_registers_[2]); \
    (edx) = static_cast<uint32_t>(__xe_cpuid_registers_[3]); \
  }
#define xe_cpu_rdtsc() __rdtsc()
#elif XE_COMPILER_CLANG || XE_COMPILER_GNUC
#include <cpuid.h>
#define xe_cpu_cpuid(level, eax, ebx, ecx, edx) \
  __cpuid((level), (eax), (ebx), (ecx), (edx));
#define xe_cpu_rdtsc() __rdtsc()
#else
#error "No cpu instruction wrappers for current compiler implemented."
#endif

#define CLOCK_FATAL(msg)                                                    \
  xe::FatalError("The raw clock source is not supported on your CPU.\n" msg \
                 "\n"                                                       \
                 "Set the cvar 'clock_source_raw' to 'false'.");




namespace xe {
// Getting the TSC frequency can be a bit tricky. This method here only works on
// Intel as it seems. There is no easy way to get the frequency outside of ring0
// on AMD, so we fail gracefully if not possible.
XE_NOINLINE
uint64_t Clock::host_tick_frequency_raw() {
  uint32_t eax, ebx, ecx, edx;

  // 00H Get max supported cpuid level.
  xe_cpu_cpuid(0x0, eax, ebx, ecx, edx);
  auto max_cpuid = eax;
  // 80000000H Get max extended cpuid level
  xe_cpu_cpuid(0x80000000, eax, ebx, ecx, edx);
  auto max_cpuid_ex = eax;

  // 80000007H Get extended power feature info
  if (max_cpuid_ex >= 0x80000007) {
    xe_cpu_cpuid(0x80000007, eax, ebx, ecx, edx);
    // Invariant TSC bit at position 8
    auto tsc_invariant = edx & (1 << 8);
    // If the TSC is not invariant it will change its frequency with power
    // states and across cores.
    if (!tsc_invariant) {
      CLOCK_FATAL("The CPU has no invariant TSC.");
      return 0;
    }
  } else {
    CLOCK_FATAL("Unclear if the CPU has an invariant TSC.")
    return 0;
  }


  
  if (max_cpuid >= 0x15) {
    // 15H Get TSC/Crystal ratio and Crystal Hz.
    xe_cpu_cpuid(0x15, eax, ebx, ecx, edx);
    uint64_t ratio_num = ebx;
    uint64_t ratio_den = eax;
    uint64_t cryst_freq = ecx;
    // For some CPUs, Crystal frequency is not reported.
    if (ratio_num && ratio_den && cryst_freq) {
      // If it is, calculate the TSC frequency
      return cryst_freq * ratio_num / ratio_den;
    }
  }

  if (max_cpuid >= 0x16) {
    // 16H Get CPU base frequency MHz in EAX.
    xe_cpu_cpuid(0x16, eax, ebx, ecx, edx);
    uint64_t cpu_base_freq = static_cast<uint64_t>(eax) * 1000000;
    assert(cpu_base_freq);
    return cpu_base_freq;
  }


  CLOCK_FATAL("The clock frequency could not be determined.");
  return 0;
}
XE_NOINLINE
uint64_t Clock::host_tick_count_raw() { return xe_cpu_rdtsc(); }

}  // namespace xe

#endif
