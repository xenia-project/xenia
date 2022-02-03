/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_LISTENER_H_
#define XENIA_UI_WINDOW_LISTENER_H_

#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

// Virtual interfaces for types that want to listen for Window events.
// Use Window::Add[Input]Listener and Window::Remove[Input]Listener to manage
// active listeners.

class WindowListener {
 public:
  virtual ~WindowListener() = default;

  // OnOpened will be followed by various initial setup listeners.
  virtual void OnOpened(UISetupEvent& e) {}
  virtual void OnClosing(UIEvent& e) {}

  virtual void OnDpiChanged(UISetupEvent& e) {}
  virtual void OnResize(UISetupEvent& e) {}

  virtual void OnGotFocus(UISetupEvent& e) {}
  virtual void OnLostFocus(UISetupEvent& e) {}

  virtual void OnFileDrop(FileDropEvent& e) {}
};

class WindowInputListener {
 public:
  virtual ~WindowInputListener() = default;

  virtual void OnKeyDown(KeyEvent& e) {}
  virtual void OnKeyUp(KeyEvent& e) {}
  virtual void OnKeyChar(KeyEvent& e) {}

  virtual void OnMouseDown(MouseEvent& e) {}
  virtual void OnMouseMove(MouseEvent& e) {}
  virtual void OnMouseUp(MouseEvent& e) {}
  virtual void OnMouseWheel(MouseEvent& e) {}
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_LISTENER_H_
