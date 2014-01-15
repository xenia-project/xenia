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

#include <xenia/core.h>


namespace xe {
namespace ui {

class Window;


class MenuItem {
public:
  MenuItem(Window* window, MenuItem* parent_item = NULL);
  virtual ~MenuItem();

  Window* window() const { return window_; }
  MenuItem* parent_item() const { return parent_item_; }

private:
  Window*   window_;
  MenuItem* parent_item_;
  // children
};


}  // namespace ui
}  // namespace xe


#endif  // XENIA_UI_MENU_ITEM_H_
