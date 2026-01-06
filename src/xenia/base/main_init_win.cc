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

#if XE_ARCH_AMD64

// Includes Windows headers, so it goes after platform_win.h.
#include "third_party/xbyak/xbyak/xbyak_util.h"

class StartupAvxCheck {
 public:
  StartupAvxCheck() {
    Xbyak::util::Cpu cpu;
    if (cpu.has(Xbyak::util::Cpu::tAVX)) {
      return;
    }
    // TODO(gibbed): detect app type and printf instead, if needed?
    MessageBoxA(
        nullptr,
        "Your CPU does not support AVX, which is required by Xenia. See the "
        "FAQ for system requirements at https://xenia.jp",
        "Xenia Error", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(static_cast<uint32_t>(-1));
  }
};

// This is a hack to get an instance of StartupAvxCheck
// constructed before any initialization code,
// where the AVX check then happens in the constructor.
//
// https://docs.microsoft.com/en-us/cpp/preprocessor/init-seg
#pragma warning(suppress : 4073)
#pragma init_seg(lib)
static StartupAvxCheck gStartupAvxCheck;

#endif