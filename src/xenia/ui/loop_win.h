/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_LOOP_WIN_H_
#define XENIA_UI_LOOP_WIN_H_

#include <list>
#include <mutex>
#include <thread>

#include "xenia/base/platform_win.h"
#include "xenia/base/threading.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace ui {

class Win32Loop : public Loop {
 public:
  Win32Loop();
  ~Win32Loop() override;

  bool is_on_loop_thread() override;

  void Post(std::function<void()> fn) override;
  void PostDelayed(std::function<void()> fn, uint64_t delay_millis) override;

  void Quit() override;
  void AwaitQuit() override;

 private:
  struct PendingTimer {
    Win32Loop* loop;
    HANDLE timer_queue;
    HANDLE timer_handle;
    std::function<void()> fn;
  };

  class PostedFn {
   public:
    explicit PostedFn(std::function<void()> fn) : fn_(std::move(fn)) {}
    void Call() { fn_(); }

   private:
    std::function<void()> fn_;
  };

  void ThreadMain();

  static void TimerQueueCallback(void* context, uint8_t);

  std::thread thread_;
  DWORD thread_id_;
  bool should_exit_ = false;
  xe::threading::Fence quit_fence_;

  HANDLE timer_queue_;
  std::mutex pending_timers_mutex_;
  std::list<PendingTimer*> pending_timers_;

  std::recursive_mutex posted_functions_mutex_;
  std::list<PostedFn> posted_functions_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_LOOP_WIN_H_
