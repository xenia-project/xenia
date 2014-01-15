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

#include <xenia/core.h>

#include <alloy/delegate.h>
#include <xenia/ui/ui_event.h>


namespace xe {
namespace ui {


class Window {
public:
  Window(xe_run_loop_ref run_loop);
  virtual ~Window();

  virtual int Initialize(const char* title, uint32_t width, uint32_t height);

  xe_run_loop_ref run_loop() const { return run_loop_; }

  const char* title() const { return title_; }
  virtual bool set_title(const char* title);
  bool is_visible() const { return is_visible_; }
  bool is_cursor_visible() const { return is_cursor_visible_; }
  virtual bool set_cursor_visible(bool value);
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  void Resize(uint32_t width, uint32_t height);

  void Close();

public:
  alloy::Delegate<UIEvent> shown;
  alloy::Delegate<UIEvent> hidden;
  alloy::Delegate<UIEvent> resizing;
  alloy::Delegate<UIEvent> resized;
  alloy::Delegate<UIEvent> closing;
  alloy::Delegate<UIEvent> closed;

  alloy::Delegate<MouseEvent> mouse_down;
  alloy::Delegate<MouseEvent> mouse_move;
  alloy::Delegate<MouseEvent> mouse_up;
  alloy::Delegate<MouseEvent> mouse_wheel;

protected:
  void OnShow();
  void OnHide();

  void BeginResizing();
  virtual bool OnResize(uint32_t width, uint32_t height);
  void EndResizing();
  virtual bool SetSize(uint32_t width, uint32_t height) = 0;

  virtual void OnClose();

private:
  xe_run_loop_ref run_loop_;
  char*     title_;
  bool      is_visible_;
  bool      is_cursor_visible_;
  bool      resizing_;
  uint32_t  width_;
  uint32_t  height_;
};


}  // namespace ui
}  // namespace xe


#endif  // XENIA_UI_WINDOW_H_
