/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/ui/control.h>

namespace poly {
namespace ui {

Control::Control(Control* parent, uint32_t flags)
    : parent_(parent),
      flags_(flags),
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
  auto control_ptr = control.get();
  children_.emplace_back(std::move(control));
  OnChildAdded(control_ptr);
}

void Control::RemoveChild(Control* child_control) {
  for (auto& it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == child_control) {
      children_.erase(it);
      OnChildRemoved(child_control);
      break;
    }
  }
}

void Control::OnResize(UIEvent& e) { on_resize(e); }

void Control::OnVisible(UIEvent& e) { on_visible(e); }

void Control::OnHidden(UIEvent& e) { on_hidden(e); }

void Control::OnGotFocus(UIEvent& e) { on_got_focus(e); }

void Control::OnLostFocus(UIEvent& e) { on_lost_focus(e); }

void Control::OnKeyDown(KeyEvent& e) { on_key_down(e); }

void Control::OnKeyUp(KeyEvent& e) { on_key_up(e); }

void Control::OnMouseDown(MouseEvent& e) { on_mouse_down(e); }

void Control::OnMouseMove(MouseEvent& e) { on_mouse_move(e); }

void Control::OnMouseUp(MouseEvent& e) { on_mouse_up(e); }

void Control::OnMouseWheel(MouseEvent& e) { on_mouse_wheel(e); }

}  // namespace ui
}  // namespace poly
