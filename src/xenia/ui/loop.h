/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_LOOP_H_
#define XENIA_UI_LOOP_H_

#include <functional>
#include <memory>

#include "xenia/base/delegate.h"
#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

class Loop {
 public:
  static std::unique_ptr<Loop> Create();

  static Loop* elemental_loop();
  // Sets the loop designated as being the main loop elemental-forms will use
  // for all activity.
  static void set_elemental_loop(Loop* loop);

  Loop();
  virtual ~Loop();

  // Returns true if the currently executing code is within the loop thread.
  virtual bool is_on_loop_thread() = 0;

  virtual void Post(std::function<void()> fn) = 0;
  virtual void PostDelayed(std::function<void()> fn, uint64_t delay_millis) = 0;
  void PostSynchronous(std::function<void()> fn);

  virtual void Quit() = 0;
  virtual void AwaitQuit() = 0;

  Delegate<UIEvent*> on_quit;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_LOOP_H_
