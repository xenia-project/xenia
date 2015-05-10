/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WIN32_WIN32_CONTROL_H_
#define XENIA_UI_WIN32_WIN32_CONTROL_H_

#include <windows.h>
#include <windowsx.h>

#include "xenia/ui/control.h"

namespace xe {
namespace ui {
namespace win32 {

class Win32Control : public Control {
 public:
  ~Win32Control() override;

  HWND hwnd() const { return hwnd_; }
  HWND parent_hwnd() const {
    return parent_ ? static_cast<Win32Control*>(parent_)->hwnd() : nullptr;
  }

  void Resize(int32_t width, int32_t height) override;
  void Resize(int32_t left, int32_t top, int32_t right,
              int32_t bottom) override;
  void ResizeToFill(int32_t pad_left, int32_t pad_top, int32_t pad_right,
                    int32_t pad_bottom) override;
  void Invalidate() override;

  void set_cursor_visible(bool value) override;
  void set_enabled(bool value) override;
  void set_visible(bool value) override;
  void set_focus(bool value) override;

 protected:
  explicit Win32Control(uint32_t flags);

  void OnCreate() override;
  void OnDestroy() override;

  void OnChildAdded(Control* child_control) override;
  void OnChildRemoved(Control* child_control) override;

  void OnResize(UIEvent& e) override;

  static LRESULT CALLBACK
  WndProcThunk(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
  virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                          LPARAM lParam);

  HWND hwnd_;

 private:
  bool HandleMouse(UINT message, WPARAM wParam, LPARAM lParam);
  bool HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam);
};

}  // namespace win32
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WIN32_WIN32_CONTROL_H_
