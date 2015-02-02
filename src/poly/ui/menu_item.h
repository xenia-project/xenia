/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_MENU_ITEM_H_
#define POLY_UI_MENU_ITEM_H_

#include <memory>
#include <vector>

#include "poly/delegate.h"
#include "poly/ui/ui_event.h"

namespace poly {
namespace ui {

class Window;

class MenuItem {
 public:
  typedef std::unique_ptr<MenuItem, void (*)(MenuItem*)> MenuItemPtr;

  enum class Type {
    kNormal,
    kSeparator,
  };

  virtual ~MenuItem();

  MenuItem* parent_item() const { return parent_item_; }

  void AddChild(MenuItem* child_item);
  void AddChild(std::unique_ptr<MenuItem> child_item);
  void AddChild(MenuItemPtr child_item);
  void RemoveChild(MenuItem* child_item);

  poly::Delegate<UIEvent> on_selected;

 protected:
  MenuItem(Type type);

  virtual void OnChildAdded(MenuItem* child_item) {}
  virtual void OnChildRemoved(MenuItem* child_item) {}

  virtual void OnSelected(UIEvent& e);

 private:
  Type type_;
  MenuItem* parent_item_;
  std::vector<MenuItemPtr> children_;
};

}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_MENU_ITEM_H_
