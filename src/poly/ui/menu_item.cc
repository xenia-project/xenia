/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/ui/menu_item.h>

namespace poly {
namespace ui {

MenuItem::MenuItem(Type type) : type_(type), parent_item_(nullptr) {}

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
  for (auto& it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == child_item) {
      children_.erase(it);
      OnChildRemoved(child_item);
      break;
    }
  }
}

void MenuItem::OnSelected(UIEvent& e) {
  on_selected(e);
}

}  // namespace ui
}  // namespace poly
