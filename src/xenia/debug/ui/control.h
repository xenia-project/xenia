/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_CONTROL_H_
#define XENIA_DEBUG_UI_CONTROL_H_

#include <memory>
#include <string>

#include "el/elements.h"
#include "el/event_handler.h"
#include "xenia/debug/ui/application.h"

namespace xe {
namespace debug {
namespace ui {

class Control {
 public:
  virtual ~Control() = default;

  el::LayoutBox* root_element() { return &root_element_; }
  xe::ui::Loop* loop() const { return Application::current()->loop(); }
  DebugClient* client() const { return client_; }
  model::System* system() const { return Application::current()->system(); }

  virtual el::Element* BuildUI() = 0;

  virtual void Setup(xe::debug::DebugClient* client) = 0;

 protected:
  Control() = default;

  el::LayoutBox root_element_;
  std::unique_ptr<el::EventHandler> handler_;
  xe::debug::DebugClient* client_ = nullptr;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_CONTROL_H_
