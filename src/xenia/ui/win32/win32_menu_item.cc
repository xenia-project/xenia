/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/win32/win32_menu_item.h"

#include "xenia/base/assert.h"

namespace xe {
namespace ui {
namespace win32 {

Win32MenuItem::Win32MenuItem(Type type, int id, const std::wstring& text)
    : id_(id), MenuItem(type, text) {
  switch (type) {
    case MenuItem::Type::kNormal:
      handle_ = CreateMenu();
      break;
    case MenuItem::Type::kPopup:
      handle_ = CreatePopupMenu();
      break;
  }
}

Win32MenuItem::Win32MenuItem(Type type) : Win32MenuItem(type, 0, L"") {}

Win32MenuItem::Win32MenuItem(Type type, const std::wstring& text)
    : Win32MenuItem(type, 0, text) {}

Win32MenuItem::~Win32MenuItem() {
  if (handle_) {
    DestroyMenu(handle_);
  }
}

void Win32MenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<Win32MenuItem*>(generic_child_item);

  switch (child_item->type()) {
    case MenuItem::Type::kPopup:
      AppendMenuW(handle_, MF_POPUP,
                  reinterpret_cast<UINT_PTR>(child_item->handle()),
                  child_item->text().c_str());
      break;
    case MenuItem::Type::kSeparator:
      AppendMenuW(handle_, MF_SEPARATOR, child_item->id(), 0);
      break;
    case MenuItem::Type::kString:
      AppendMenuW(handle_, MF_STRING, child_item->id(),
                  child_item->text().c_str());
      break;
  }
}

void Win32MenuItem::OnChildRemoved(MenuItem* generic_child_item) {
  auto child_item = static_cast<Win32MenuItem*>(generic_child_item);
  //
}

}  // namespace win32
}  // namespace ui
}  // namespace xe
