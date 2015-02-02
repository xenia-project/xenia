/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_WIN32_WIN32_MENU_ITEM_H_
#define POLY_UI_WIN32_WIN32_MENU_ITEM_H_

#include <windows.h>
#include <windowsx.h>

#include "poly/ui/menu_item.h"

namespace poly {
namespace ui {
namespace win32 {

class Win32MenuItem : public MenuItem {
 public:
  ~Win32MenuItem() override;

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  Win32MenuItem(Type type);

  HMENU handle_;
};

}  // namespace win32
}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_WIN32_WIN32_MENU_ITEM_H_
