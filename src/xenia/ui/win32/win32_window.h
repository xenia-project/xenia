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

#include <xenia/core.h>

#include <xenia/ui/window.h>


namespace xe {
namespace ui {
namespace win32 {


class Win32Window : public Window {
public:
  Win32Window(xe_run_loop_ref run_loop);
  virtual ~Win32Window();

  virtual int Initialize(const char* title, uint32_t width, uint32_t height);

  virtual bool set_title(const char* title);
  virtual bool set_cursor_visible(bool value);
  HWND handle() const { return handle_; }

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

protected:
  virtual bool SetSize(uint32_t width, uint32_t height);
  virtual void OnClose();

private:
  void EnableMMCSS();
  bool HandleMouse(UINT message, WPARAM wParam, LPARAM lParam);
  bool HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam);

  HWND  handle_;
  bool  closing_;
};


}  // namespace win32
}  // namespace ui
}  // namespace xe


#endif  // XENIA_UI_WIN32_WIN32_WINDOW_H_
