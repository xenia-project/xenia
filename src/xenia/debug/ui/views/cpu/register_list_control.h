/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_VIEWS_CPU_REGISTER_LIST_CONTROL_H_
#define XENIA_DEBUG_UI_VIEWS_CPU_REGISTER_LIST_CONTROL_H_

#include <memory>
#include <string>

#include "xenia/debug/ui/control.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

class RegisterItemSource;

enum class RegisterSet {
  kGeneral,
  kFloat,
  kVector,
  kHost,
};

class RegisterListControl : public Control {
 public:
  explicit RegisterListControl(RegisterSet set);
  ~RegisterListControl() override;

  el::Element* BuildUI() override;

  void Setup(xe::debug::DebugClient* client) override;

  model::Thread* thread() const { return thread_; }
  void set_thread(model::Thread* thread);

 private:
  void InvalidateRegisters();

  RegisterSet set_;
  model::Thread* thread_ = nullptr;
  std::unique_ptr<RegisterItemSource> item_source_;
};

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_VIEWS_CPU_REGISTER_LIST_CONTROL_H_
