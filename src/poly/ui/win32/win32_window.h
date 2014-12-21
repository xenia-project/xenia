/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_UI_WIN32_WIN32_WINDOW_H_
#define POLY_UI_WIN32_WIN32_WINDOW_H_

#include <string>

#include <poly/ui/win32/win32_control.h>
#include <poly/ui/window.h>

namespace poly {
namespace ui {
namespace win32 {

class Win32Window : public Window<Win32Control> {
 public:
  Win32Window(const std::wstring& title);
  ~Win32Window() override;

  bool Initialize() override;

  bool set_title(const std::wstring& title) override;

  void Resize(int32_t width, int32_t height) override;
  void Resize(int32_t left, int32_t top, int32_t right,
              int32_t bottom) override;
  void ResizeToFill(int32_t pad_left, int32_t pad_top, int32_t pad_right,
                    int32_t pad_bottom) override;

 protected:
  bool CreateHWND() override;

  void OnClose() override;

  LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                  LPARAM lParam) override;

 private:
  void EnableMMCSS();

  HMENU main_menu_;
  bool closing_;
};

}  // namespace win32
}  // namespace ui
}  // namespace poly

#endif  // POLY_UI_WIN32_WIN32_WINDOW_H_
