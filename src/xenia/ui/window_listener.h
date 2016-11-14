/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_LISTENER_H_
#define XENIA_UI_WINDOW_LISTENER_H_

#include "xenia/ui/ui_event.h"

namespace xe {
namespace ui {

// Virtual interface for types that want to listen for Window events.
// Use Window::AttachListener and Window::DetachListener to manage active
// listeners.
class WindowListener {
 public:
  virtual ~WindowListener() = default;

  virtual void OnMainMenuChange() {}
  virtual void OnClosing(UIEvent* e) {}
  virtual void OnClosed(UIEvent* e) {}

  virtual void OnResize(UIEvent* e) {}
  virtual void OnLayout(UIEvent* e) {}
  virtual void OnPainting(UIEvent* e) {}
  virtual void OnPaint(UIEvent* e) {}
  virtual void OnPainted(UIEvent* e) {}
  virtual void OnFileDrop(UIEvent* e) {}

  virtual void OnVisible(UIEvent* e) {}
  virtual void OnHidden(UIEvent* e) {}

  virtual void OnGotFocus(UIEvent* e) {}
  virtual void OnLostFocus(UIEvent* e) {}

  virtual void OnKeyDown(KeyEvent* e) {}
  virtual void OnKeyUp(KeyEvent* e) {}
  virtual void OnKeyChar(KeyEvent* e) {}

  virtual void OnMouseDown(MouseEvent* e) {}
  virtual void OnMouseMove(MouseEvent* e) {}
  virtual void OnMouseUp(MouseEvent* e) {}
  virtual void OnMouseWheel(MouseEvent* e) {}
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_LISTENER_H_
