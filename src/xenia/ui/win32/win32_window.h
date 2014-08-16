/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WIN32_WIN32_WINDOW_H_
#define XENIA_UI_WIN32_WIN32_WINDOW_H_

#include <string>

#include <xenia/core.h>

#include <xenia/ui/window.h>

namespace xe {
namespace ui {
namespace win32 {

class Win32Window : public Window {
 public:
  Win32Window(xe_run_loop_ref run_loop);
  ~Win32Window() override;

  int Initialize(const std::wstring& title, uint32_t width,
                 uint32_t height) override;

  bool set_title(const std::wstring& title) override;
  bool set_cursor_visible(bool value) override;
  HWND handle() const { return handle_; }

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

 protected:
  bool SetSize(uint32_t width, uint32_t height) override;
  void OnClose() override;

 private:
  void EnableMMCSS();
  bool HandleMouse(UINT message, WPARAM wParam, LPARAM lParam);
  bool HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam);

  HWND handle_;
  bool closing_;
};

}  // namespace win32
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WIN32_WIN32_WINDOW_H_
