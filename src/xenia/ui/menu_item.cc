/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/menu_item.h"

namespace xe {
namespace ui {

std::unique_ptr<MenuItem> MenuItem::Create(Type type) {
  return MenuItem::Create(type, L"", L"", nullptr);
}

std::unique_ptr<MenuItem> MenuItem::Create(Type type,
                                           const std::wstring& text) {
  return MenuItem::Create(type, text, L"", nullptr);
}

std::unique_ptr<MenuItem> MenuItem::Create(Type type, const std::wstring& text,
                                           std::function<void()> callback) {
  return MenuItem::Create(type, text, L"", std::move(callback));
}

MenuItem::MenuItem(Type type, const std::wstring& text,
                   const std::wstring& hotkey, std::function<void()> callback)
    : type_(type),
      parent_item_(nullptr),
      text_(text),
      hotkey_(hotkey),
      callback_(std::move(callback)) {}

MenuItem::~MenuItem() = default;

void MenuItem::AddChild(MenuItem* child_item) {
  AddChild(MenuItemPtr(child_item, [](MenuItem* item) {}));
}

void MenuItem::AddChild(std::unique_ptr<MenuItem> child_item) {
  AddChild(
      MenuItemPtr(child_item.release(), [](MenuItem* item) { delete item; }));
}

void MenuItem::AddChild(MenuItemPtr child_item) {
  auto child_item_ptr = child_item.get();
  children_.emplace_back(std::move(child_item));
  OnChildAdded(child_item_ptr);
}

void MenuItem::RemoveChild(MenuItem* child_item) {
  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == child_item) {
      children_.erase(it);
      OnChildRemoved(child_item);
      break;
    }
  }
}

MenuItem* MenuItem::child(size_t index) { return children_[index].get(); }

void MenuItem::OnSelected(UIEvent* e) {
  if (callback_) {
    callback_();
  }
}

}  // namespace ui
}  // namespace xe
