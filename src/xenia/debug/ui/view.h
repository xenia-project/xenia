/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_VIEW_H_
#define XENIA_DEBUG_UI_VIEW_H_

#include <memory>
#include <string>

#include "el/elements.h"

namespace xe {
namespace debug {
namespace ui {

class View {
 public:
  virtual ~View() = default;

  std::string name() const { return name_; }
  el::LayoutBox* root_element() { return &root_element_; }

  virtual el::Element* BuildUI() = 0;

 protected:
  View(std::string name) : name_(name) {}

  std::string name_;
  el::LayoutBox root_element_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_VIEW_H_
