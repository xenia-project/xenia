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

#include <windows.h>
#include <windowsx.h>

#include "xenia/ui/menu_item.h"

namespace xe {
namespace ui {
namespace win32 {

class Win32MenuItem : public MenuItem {
 public:
  Win32MenuItem(Type type);
  Win32MenuItem(Type type, const std::wstring& text,
                const std::wstring& hotkey = L"");
  Win32MenuItem(Type type, int id, const std::wstring& text,
                const std::wstring& hotkey = L"");
  ~Win32MenuItem() override;

  HMENU handle() { return handle_; }
  int id() { return id_; }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  HMENU handle_;
  uint32_t position_;  // Position within parent, if any
  int id_;
};

}  // namespace win32
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WIN32_WIN32_MENU_ITEM_H_
