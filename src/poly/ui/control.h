/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_CONTROL_H_
#define POLY_UI_CONTROL_H_

#include <memory>
#include <vector>

#include "poly/delegate.h"
#include "poly/ui/ui_event.h"

namespace poly {
namespace ui {

class Control {
 public:
  typedef std::unique_ptr<Control, void (*)(Control*)> ControlPtr;
  enum Flags {
    // Control paints itself, so disable platform drawing.
    kFlagOwnPaint = 1 << 1,
  };

  virtual ~Control();

  Control* parent() const { return parent_; }

  size_t child_count() const { return children_.size(); }
  Control* child(size_t i) const { return children_[i].get(); }
  void AddChild(Control* child_control);
  void AddChild(std::unique_ptr<Control> child_control);
  void AddChild(ControlPtr child_control);
  ControlPtr RemoveChild(Control* child_control);

  int32_t width() const { return width_; }
  int32_t height() const { return height_; }
  virtual void Resize(int32_t width, int32_t height) = 0;
  virtual void Resize(int32_t left, int32_t top, int32_t right,
                      int32_t bottom) = 0;
  void ResizeToFill() { ResizeToFill(0, 0, 0, 0); }
  virtual void ResizeToFill(int32_t pad_left, int32_t pad_top,
                            int32_t pad_right, int32_t pad_bottom) = 0;
  void Layout();
  virtual void Invalidate() {}

  // TODO(benvanik): colors/brushes/etc.
  // TODO(benvanik): fonts.

  bool is_cursor_visible() const { return is_cursor_visible_; }
  virtual void set_cursor_visible(bool value) { is_cursor_visible_ = value; }

  bool is_enabled() const { return is_enabled_; }
  virtual void set_enabled(bool value) { is_enabled_ = value; }

  bool is_visible() const { return is_visible_; }
  virtual void set_visible(bool value) { is_visible_ = value; }

  bool has_focus() const { return has_focus_; }
  virtual void set_focus(bool value) { has_focus_ = value; }

 public:
  poly::Delegate<UIEvent> on_resize;
  poly::Delegate<UIEvent> on_layout;
  poly::Delegate<UIEvent> on_paint;

  poly::Delegate<UIEvent> on_visible;
  poly::Delegate<UIEvent> on_hidden;

  poly::Delegate<UIEvent> on_got_focus;
  poly::Delegate<UIEvent> on_lost_focus;

  poly::Delegate<KeyEvent> on_key_down;
  poly::Delegate<KeyEvent> on_key_up;
  poly::Delegate<KeyEvent> on_key_char;

  poly::Delegate<MouseEvent> on_mouse_down;
  poly::Delegate<MouseEvent> on_mouse_move;
  poly::Delegate<MouseEvent> on_mouse_up;
  poly::Delegate<MouseEvent> on_mouse_wheel;

 protected:
  explicit Control(uint32_t flags);

  virtual bool Create() = 0;
  virtual void Destroy() {}

  virtual void OnCreate() {}
  virtual void OnDestroy() {}

  virtual void OnChildAdded(Control* child_control) {}
  virtual void OnChildRemoved(Control* child_control) {}

  virtual void OnResize(UIEvent& e);
  virtual void OnLayout(UIEvent& e);
  virtual void OnPaint(UIEvent& e);

  virtual void OnVisible(UIEvent& e);
  virtual void OnHidden(UIEvent& e);

  virtual void OnGotFocus(UIEvent& e);
  virtual void OnLostFocus(UIEvent& e);

  virtual void OnKeyDown(KeyEvent& e);
  virtual void OnKeyUp(KeyEvent& e);
  virtual void OnKeyChar(KeyEvent& e);

  virtual void OnMouseDown(MouseEvent& e);
  virtual void OnMouseMove(MouseEvent& e);
  virtual void OnMouseUp(MouseEvent& e);
  virtual void OnMouseWheel(MouseEvent& e);

  uint32_t flags_;
  Control* parent_;
  std::vector<ControlPtr> children_;

  int32_t width_;
  int32_t height_;

  bool is_cursor_visible_;
  bool is_enabled_;
  bool is_visible_;
  bool has_focus_;
};

}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_CONTROL_H_
