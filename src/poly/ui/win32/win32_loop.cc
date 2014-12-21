/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/ui/win32/win32_loop.h>

#include <poly/assert.h>

namespace poly {
namespace ui {
namespace win32 {

const DWORD kWmWin32LoopPost = WM_APP + 0x100;
const DWORD kWmWin32LoopQuit = WM_APP + 0x101;

class PostedFn {
 public:
  explicit PostedFn(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Call() { fn_(); }

 private:
  std::function<void()> fn_;
};

Win32Loop::Win32Loop() : thread_id_(0) {
  poly::threading::Fence init_fence;
  thread_ = std::thread([&]() {
    poly::threading::set_name("Win32 Loop");
    thread_id_ = GetCurrentThreadId();

    init_fence.Signal();

    ThreadMain();

    quit_fence_.Signal();
  });
  init_fence.Wait();
}

Win32Loop::~Win32Loop() = default;

void Win32Loop::ThreadMain() {
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    switch (msg.message) {
      case kWmWin32LoopPost:
        if (msg.wParam == reinterpret_cast<WPARAM>(this)) {
          auto posted_fn = reinterpret_cast<PostedFn*>(msg.lParam);
          posted_fn->Call();
          delete posted_fn;
        }
        break;
      case kWmWin32LoopQuit:
        if (msg.wParam == reinterpret_cast<WPARAM>(this)) {
          return;
        }
        break;
    }
  }
}

void Win32Loop::Post(std::function<void()> fn) {
  assert_true(thread_id_ != 0);
  PostThreadMessage(thread_id_, kWmWin32LoopPost,
                    reinterpret_cast<WPARAM>(this),
                    reinterpret_cast<LPARAM>(new PostedFn(std::move(fn))));
}

void Win32Loop::Quit() {
  assert_true(thread_id_ != 0);
  PostThreadMessage(thread_id_, kWmWin32LoopQuit,
                    reinterpret_cast<WPARAM>(this), 0);
}

void Win32Loop::AwaitQuit() {
  quit_fence_.Wait();
}

}  // namespace win32
}  // namespace ui
}  // namespace poly
