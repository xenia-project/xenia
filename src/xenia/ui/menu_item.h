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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

class Window;

class MenuItem {
 public:
  typedef std::unique_ptr<MenuItem, void (*)(MenuItem*)> MenuItemPtr;

  enum class Type {
    kPopup,  // Popup menu (submenu)
    kSeparator,
    kNormal,  // Root menu
    kString,  // Menu is just a string
  };

  static std::unique_ptr<MenuItem> Create(Type type);
  static std::unique_ptr<MenuItem> Create(Type type, const std::wstring& text);
  static std::unique_ptr<MenuItem> Create(Type type, const std::wstring& text,
                                          std::function<void()> callback);
  static std::unique_ptr<MenuItem> Create(Type type, const std::wstring& text,
                                          const std::wstring& hotkey,
                                          std::function<void()> callback);

  virtual ~MenuItem();

  MenuItem* parent_item() const { return parent_item_; }
  Type type() { return type_; }
  const std::wstring& text() { return text_; }
  const std::wstring& hotkey() { return hotkey_; }

  void AddChild(MenuItem* child_item);
  void AddChild(std::unique_ptr<MenuItem> child_item);
  void AddChild(MenuItemPtr child_item);
  void RemoveChild(MenuItem* child_item);
  MenuItem* child(size_t index);

  virtual void EnableMenuItem(Window& window) = 0;
  virtual void DisableMenuItem(Window& window) = 0;

 protected:
  MenuItem(Type type, const std::wstring& text, const std::wstring& hotkey,
           std::function<void()> callback);

  virtual void OnChildAdded(MenuItem* child_item) {}
  virtual void OnChildRemoved(MenuItem* child_item) {}

  virtual void OnSelected(UIEvent* e);

  Type type_;
  MenuItem* parent_item_;
  std::vector<MenuItemPtr> children_;
  std::wstring text_;
  std::wstring hotkey_;
  std::function<void()> callback_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MENU_ITEM_H_
