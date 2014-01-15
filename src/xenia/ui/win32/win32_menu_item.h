/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WIN32_WIN32_MENU_ITEM_H_
#define XENIA_UI_WIN32_WIN32_MENU_ITEM_H_

#include <xenia/core.h>

#include <xenia/ui/menu_item.h>


namespace xe {
namespace ui {
namespace win32 {


class Win32MenuItem : public MenuItem {
public:
  Win32MenuItem(Window* window, MenuItem* parent_item = NULL);
  virtual ~Win32MenuItem();

private:
};


}  // namespace win32
}  // namespace ui
}  // namespace xe


#endif  // XENIA_UI_WIN32_WIN32_MENU_ITEM_H_
