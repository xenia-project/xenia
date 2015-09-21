/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/loop_win.h"

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

const DWORD kWmWin32LoopPost = WM_APP + 0x100;
const DWORD kWmWin32LoopQuit = WM_APP + 0x101;

class PostedFn {
 public:
  explicit PostedFn(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Call() { fn_(); }

 private:
  std::function<void()> fn_;
};

std::unique_ptr<Loop> Loop::Create() { return std::make_unique<Win32Loop>(); }

Win32Loop::Win32Loop() : thread_id_(0) {
  timer_queue_ = CreateTimerQueue();

  xe::threading::Fence init_fence;
  thread_ = std::thread([&init_fence, this]() {
    xe::threading::set_name("Win32 Loop");
    thread_id_ = GetCurrentThreadId();

    // Make a Win32 call to enable the thread queue.
    MSG msg;
    PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    init_fence.Signal();

    ThreadMain();

    quit_fence_.Signal();
  });
  init_fence.Wait();
}

Win32Loop::~Win32Loop() {
  Quit();
  thread_.join();

  DeleteTimerQueueEx(timer_queue_, INVALID_HANDLE_VALUE);
  std::lock_guard<std::mutex> lock(pending_timers_mutex_);
  while (!pending_timers_.empty()) {
    auto timer = pending_timers_.back();
    pending_timers_.pop_back();
    delete timer;
  }
}

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

  UIEvent e(nullptr);
  on_quit(&e);
}

bool Win32Loop::is_on_loop_thread() {
  return thread_id_ == GetCurrentThreadId();
}

void Win32Loop::Post(std::function<void()> fn) {
  assert_true(thread_id_ != 0);
  if (!PostThreadMessage(
          thread_id_, kWmWin32LoopPost, reinterpret_cast<WPARAM>(this),
          reinterpret_cast<LPARAM>(new PostedFn(std::move(fn))))) {
    assert_always("Unable to post message to thread queue");
  }
}

void Win32Loop::TimerQueueCallback(void* context, uint8_t) {
  auto timer = reinterpret_cast<PendingTimer*>(context);
  auto loop = timer->loop;
  auto fn = std::move(timer->fn);
  DeleteTimerQueueTimer(timer->timer_queue, timer->timer_handle, NULL);
  {
    std::lock_guard<std::mutex> lock(loop->pending_timers_mutex_);
    loop->pending_timers_.remove(timer);
  }
  delete timer;
  loop->Post(std::move(fn));
}

void Win32Loop::PostDelayed(std::function<void()> fn, uint64_t delay_millis) {
  if (!delay_millis) {
    Post(std::move(fn));
    return;
  }
  auto timer = new PendingTimer();
  timer->loop = this;
  timer->timer_queue = timer_queue_;
  timer->fn = std::move(fn);
  {
    std::lock_guard<std::mutex> lock(pending_timers_mutex_);
    pending_timers_.push_back(timer);
  }
  CreateTimerQueueTimer(&timer->timer_handle, timer_queue_,
                        WAITORTIMERCALLBACK(TimerQueueCallback), timer,
                        DWORD(delay_millis), 0,
                        WT_EXECUTEINTIMERTHREAD | WT_EXECUTEONLYONCE);
}

void Win32Loop::Quit() {
  assert_true(thread_id_ != 0);
  PostThreadMessage(thread_id_, kWmWin32LoopQuit,
                    reinterpret_cast<WPARAM>(this), 0);
}

void Win32Loop::AwaitQuit() { quit_fence_.Wait(); }

}  // namespace ui
}  // namespace xe
