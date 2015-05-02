/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/control.h"

#include "poly/assert.h"

namespace xe {
namespace ui {

Control::Control(uint32_t flags)
    : flags_(flags),
      parent_(nullptr),
      width_(0),
      height_(0),
      is_cursor_visible_(true),
      is_enabled_(true),
      is_visible_(true),
      has_focus_(false) {}

Control::~Control() { children_.clear(); }

void Control::AddChild(Control* child_control) {
  AddChild(ControlPtr(child_control, [](Control* control) {}));
}

void Control::AddChild(std::unique_ptr<Control> child_control) {
  AddChild(ControlPtr(child_control.release(),
                      [](Control* control) { delete control; }));
}

void Control::AddChild(ControlPtr control) {
  assert_null(control->parent());
  control->parent_ = this;
  auto control_ptr = control.get();
  children_.emplace_back(std::move(control));
  OnChildAdded(control_ptr);
}

Control::ControlPtr Control::RemoveChild(Control* child_control) {
  assert_true(child_control->parent() == this);
  for (auto& it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == child_control) {
      auto control_ptr = std::move(*it);
      child_control->parent_ = nullptr;
      children_.erase(it);
      OnChildRemoved(child_control);
      return control_ptr;
    }
  }
  return ControlPtr(nullptr, [](Control*) {});
}

void Control::Layout() {
  auto e = UIEvent(this);
  OnLayout(e);
  for (auto& child_control : children_) {
    child_control->OnLayout(e);
  }
}

void Control::OnResize(UIEvent& e) { on_resize(e); }

void Control::OnLayout(UIEvent& e) { on_layout(e); }

void Control::OnPaint(UIEvent& e) { on_paint(e); }

void Control::OnVisible(UIEvent& e) { on_visible(e); }

void Control::OnHidden(UIEvent& e) { on_hidden(e); }

void Control::OnGotFocus(UIEvent& e) { on_got_focus(e); }

void Control::OnLostFocus(UIEvent& e) { on_lost_focus(e); }

void Control::OnKeyDown(KeyEvent& e) {
  on_key_down(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnKeyDown(e);
  }
}

void Control::OnKeyUp(KeyEvent& e) {
  on_key_up(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnKeyUp(e);
  }
}

void Control::OnKeyChar(KeyEvent& e) {
  on_key_char(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnKeyChar(e);
  }
}

void Control::OnMouseDown(MouseEvent& e) {
  on_mouse_down(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnMouseDown(e);
  }
}

void Control::OnMouseMove(MouseEvent& e) {
  on_mouse_move(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnMouseMove(e);
  }
}

void Control::OnMouseUp(MouseEvent& e) {
  on_mouse_up(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnMouseUp(e);
  }
}

void Control::OnMouseWheel(MouseEvent& e) {
  on_mouse_wheel(e);
  if (parent_ && !e.is_handled()) {
    parent_->OnMouseWheel(e);
  }
}

}  // namespace ui
}  // namespace xe
