/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_VIEWS_CPU_CALL_STACK_CONTROL_H_
#define XENIA_DEBUG_UI_VIEWS_CPU_CALL_STACK_CONTROL_H_

#include <memory>
#include <string>

#include "xenia/debug/ui/control.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

class CallStackItemSource;

class CallStackControl : public Control {
 public:
  CallStackControl();
  ~CallStackControl() override;

  el::Element* BuildUI() override;

  void Setup(xe::debug::DebugClient* client) override;

  model::Thread* thread() const { return thread_; }
  void set_thread(model::Thread* thread);

 private:
  void InvalidateCallStack();

  model::Thread* thread_ = nullptr;
  std::unique_ptr<CallStackItemSource> item_source_;
};

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_VIEWS_CPU_CALL_STACK_CONTROL_H_
