/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/win32/win32_loop.h>

namespace xe {
namespace ui {
namespace win32 {

const DWORD kWmWin32LoopQuit = WM_APP + 0x100;

Win32Loop::Win32Loop() {}

Win32Loop::~Win32Loop() {}

bool Win32Loop::Run() {
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    switch (msg.message) {
      case kWmWin32LoopQuit:
        if (msg.wParam == reinterpret_cast<WPARAM>(this)) {
          return true;
        }
        break;
    }
  }
  return true;
}

void Win32Loop::Quit() {
  PostMessage(nullptr, kWmWin32LoopQuit, reinterpret_cast<WPARAM>(this), 0);
}

}  // namespace win32
}  // namespace ui
}  // namespace xe
