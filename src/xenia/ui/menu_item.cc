/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/menu_item.h>

namespace xe {
namespace ui {

MenuItem::MenuItem(Window* window) : window_(window), parent_item_(nullptr) {}

MenuItem::~MenuItem() {}

void MenuItem::AddChild(std::unique_ptr<MenuItem> child_item) {
  children_.emplace_back(std::move(child_item));
}

}  // namespace ui
}  // namespace xe
