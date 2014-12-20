/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_MENU_ITEM_H_
#define XENIA_UI_MENU_ITEM_H_

#include <memory>
#include <vector>

#include <xenia/common.h>

namespace xe {
namespace ui {

class Window;

class MenuItem {
 public:
  MenuItem(Window* window);
  virtual ~MenuItem();

  Window* window() const { return window_; }
  MenuItem* parent_item() const { return parent_item_; }

  virtual void AddChild(std::unique_ptr<MenuItem> child_item);

 private:
  Window* window_;
  MenuItem* parent_item_;
  std::vector<std::unique_ptr<MenuItem>> children_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MENU_ITEM_H_
