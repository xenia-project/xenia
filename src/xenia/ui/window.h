/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_H_
#define XENIA_UI_WINDOW_H_

#include <string>

#include "xenia/base/delegate.h"
#include "xenia/ui/control.h"
#include "xenia/ui/loop.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

template <typename T>
class Window : public T {
 public:
  ~Window() override = default;

  virtual bool Initialize() { return true; }

  Loop* loop() const { return loop_; }

  const std::wstring& title() const { return title_; }
  virtual bool set_title(const std::wstring& title) {
    if (title == title_) {
      return false;
    }
    title_ = title;
    return true;
  }

  MenuItem* menu() const { return menu_; }
  void set_menu(MenuItem* menu) {
    if (menu == menu_) {
      return;
    }
    menu_ = menu;
    OnSetMenu(menu);
  }

  void Close() {
    auto e = UIEvent(this);
    on_closing(e);

    OnClose();

    e = UIEvent(this);
    on_closed(e);
  }

  virtual bool is_fullscreen() const { return false; }
  virtual void ToggleFullscreen(bool fullscreen) {}

 public:
  Delegate<UIEvent> on_shown;
  Delegate<UIEvent> on_hidden;
  Delegate<UIEvent> on_closing;
  Delegate<UIEvent> on_closed;

 protected:
  Window(Loop* loop, const std::wstring& title)
      : T(0), loop_(loop), title_(title) {}

  void OnShow() {
    if (is_visible_) {
      return;
    }
    is_visible_ = true;
    auto e = UIEvent(this);
    on_shown(e);
  }

  void OnHide() {
    if (!is_visible_) {
      return;
    }
    is_visible_ = false;
    auto e = UIEvent(this);
    on_hidden(e);
  }

  virtual void OnClose() {}

  virtual void OnSetMenu(MenuItem* menu) {}

  virtual void OnCommand(int id) {}

  Loop* loop_ = nullptr;
  std::wstring title_;
  MenuItem* menu_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_H_
