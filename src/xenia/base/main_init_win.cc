/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform_win.h"

#include <cstdlib>

// Includes Windows headers, so it goes after platform_win.h.
#include "third_party/xbyak/xbyak/xbyak_util.h"

class StartupCpuFeatureCheck {
 public:
  StartupCpuFeatureCheck() {
    Xbyak::util::Cpu cpu;
    const char* error_message = nullptr;
    if (!cpu.has(Xbyak::util::Cpu::tAVX)) {
      error_message =
          "Your CPU does not support AVX, which is required by Xenia. See "
          "the "
          "FAQ for system requirements at https://xenia.jp";
    }
#if 0
    if (!error_message) {
      unsigned int data[4];
      Xbyak::util::Cpu::getCpuid(0x80000001, data);
      if (!(data[2] & (1U << 8))) {
        error_message =
            "Your cpu does not support PrefetchW, which Xenia Canary "
            "requires.";
      }
    }
#endif
    if (error_message == nullptr) {
      return;
    } else {
      // TODO(gibbed): detect app type and printf instead, if needed?
      MessageBoxA(nullptr, error_message, "Xenia Error",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
      ExitProcess(static_cast<uint32_t>(-1));
    }
  }
};

// This is a hack to get an instance of StartupAvxCheck
// constructed before any initialization code,
// where the AVX check then happens in the constructor.
//
// https://docs.microsoft.com/en-us/cpp/preprocessor/init-seg
#pragma warning(suppress : 4073)
#pragma init_seg(lib)
static StartupCpuFeatureCheck gStartupAvxCheck;