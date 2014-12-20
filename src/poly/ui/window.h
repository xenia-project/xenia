/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_WINDOW_H_
#define POLY_UI_WINDOW_H_

#include <string>

#include <poly/delegate.h>
#include <poly/ui/control.h>
#include <poly/ui/ui_event.h>

namespace poly {
namespace ui {

template <typename T>
class Window : public T {
 public:
  ~Window() override = default;

  virtual bool Initialize() { return true; }

  const std::wstring& title() const { return title_; }
  virtual bool set_title(const std::wstring& title) {
    if (title == title_) {
      return false;
    }
    title_ = title;
    return true;
  }

  void Close() {
    auto e = UIEvent(this);
    on_closing(e);

    OnClose();

    e = UIEvent(this);
    on_closed(e);
  }

 public:
  poly::Delegate<UIEvent> on_shown;
  poly::Delegate<UIEvent> on_hidden;
  poly::Delegate<UIEvent> on_closing;
  poly::Delegate<UIEvent> on_closed;

 protected:
  Window(const std::wstring& title) : T(nullptr, 0), title_(title) {}

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

 private:
  std::wstring title_;
};

}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_WINDOW_H_
