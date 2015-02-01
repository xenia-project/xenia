/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_WIN32_WIN32_LOOP_H_
#define POLY_UI_WIN32_WIN32_LOOP_H_

#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "poly/threading.h"
#include "poly/ui/loop.h"

namespace poly {
namespace ui {
namespace win32 {

class Win32Loop : public Loop {
 public:
  Win32Loop();
  ~Win32Loop() override;

  void Post(std::function<void()> fn) override;

  void Quit() override;
  void AwaitQuit() override;

 private:
  void ThreadMain();

  std::thread thread_;
  DWORD thread_id_;
  poly::threading::Fence quit_fence_;
};

}  // namespace win32
}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_WIN32_WIN32_LOOP_H_
