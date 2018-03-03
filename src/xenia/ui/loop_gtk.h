/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_LOOP_GTK_H_
#define XENIA_UI_LOOP_GTK_H_

#include <list>
#include <mutex>
#include <thread>

#include "xenia/base/platform_linux.h"
#include "xenia/base/threading.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace ui {

class GTKWindow;

class GTKLoop : public Loop {
 public:
  GTKLoop();
  ~GTKLoop() override;

  bool is_on_loop_thread() override;

  void Post(std::function<void()> fn) override;
  void PostDelayed(std::function<void()> fn, uint64_t delay_millis) override;

  void Quit() override;
  void AwaitQuit() override;

 private:
  void ThreadMain();

  std::thread::id thread_id_;
  std::thread thread_;
  xe::threading::Fence quit_fence_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_LOOP_GTK_H_
