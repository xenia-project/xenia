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

#include <poly/delegate.h>
#include <xenia/common.h>
#include <xenia/ui/ui_event.h>

namespace xe {
namespace ui {

class Window {
 public:
  Window();
  virtual ~Window();

  virtual int Initialize(const std::wstring& title, uint32_t width,
                         uint32_t height);

  const std::wstring& title() const { return title_; }
  virtual bool set_title(const std::wstring& title);
  bool is_visible() const { return is_visible_; }
  bool is_cursor_visible() const { return is_cursor_visible_; }
  virtual bool set_cursor_visible(bool value);
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  void Resize(uint32_t width, uint32_t height);

  void Close();

 public:
  poly::Delegate<UIEvent> shown;
  poly::Delegate<UIEvent> hidden;
  poly::Delegate<UIEvent> resizing;
  poly::Delegate<UIEvent> resized;
  poly::Delegate<UIEvent> closing;
  poly::Delegate<UIEvent> closed;

  poly::Delegate<KeyEvent> key_down;
  poly::Delegate<KeyEvent> key_up;

  poly::Delegate<MouseEvent> mouse_down;
  poly::Delegate<MouseEvent> mouse_move;
  poly::Delegate<MouseEvent> mouse_up;
  poly::Delegate<MouseEvent> mouse_wheel;

 protected:
  void OnShow();
  void OnHide();

  void BeginResizing();
  virtual bool OnResize(uint32_t width, uint32_t height);
  void EndResizing();
  virtual bool SetSize(uint32_t width, uint32_t height) = 0;

  virtual void OnClose();

 private:
  std::wstring title_;
  bool is_visible_;
  bool is_cursor_visible_;
  bool resizing_;
  uint32_t width_;
  uint32_t height_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_H_
