/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_WIN32_WINDOW_H_
#define XENIA_CORE_WIN32_WINDOW_H_

#include <xenia/common.h>
#include <xenia/core/window.h>


namespace xe {
namespace core {


class Win32Window : public Window {
public:
  Win32Window(xe_run_loop_ref run_loop);
  virtual ~Win32Window();

  HWND handle() const { return handle_; }

  virtual void set_title(const xechar_t* title);

  virtual void Resize(uint32_t width, uint32_t height);

  virtual void ShowError(const xechar_t* message);

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

protected:
  HWND  handle_;

private:
  void Create();
  void EnableMMCSS();
};


}  // namespace core
}  // namespace xe


#endif  // XENIA_CORE_WIN32_WINDOW_H_
