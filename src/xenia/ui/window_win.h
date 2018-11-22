/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_WIN_H_
#define XENIA_UI_WINDOW_WIN_H_

#include <memory>
#include <string>

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

class Win32Window : public Window {
  using super = Window;

 public:
  Win32Window(Loop* loop, const std::wstring& title);
  ~Win32Window() override;

  NativePlatformHandle native_platform_handle() const override;
  NativeWindowHandle native_handle() const override { return hwnd_; }
  HWND hwnd() const { return hwnd_; }

  void EnableMainMenu() override;
  void DisableMainMenu() override;

  bool set_title(const std::wstring& title) override;

  bool SetIcon(const void* buffer, size_t size) override;

  bool is_fullscreen() const override;
  void ToggleFullscreen(bool fullscreen) override;

  bool is_bordered() const override;
  void set_bordered(bool enabled) override;

  int get_dpi() const override;

  void set_cursor_visible(bool value) override;
  void set_focus(bool value) override;

  void Resize(int32_t width, int32_t height) override;
  void Resize(int32_t left, int32_t top, int32_t right,
              int32_t bottom) override;

  // (raw) Resize the window, no DPI scaling applied.
  void RawReposition(const RECT& rc);

  bool Initialize() override;
  void Invalidate() override;
  void Close() override;

 protected:
  bool OnCreate() override;
  void OnMainMenuChange() override;
  void OnDestroy() override;
  void OnClose() override;

  void OnResize(UIEvent* e) override;

  static LRESULT CALLBACK WndProcThunk(HWND hWnd, UINT message, WPARAM wParam,
                                       LPARAM lParam);
  virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                          LPARAM lParam);

 private:
  void EnableMMCSS();
  bool HandleMouse(UINT message, WPARAM wParam, LPARAM lParam);
  bool HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam);

  HWND hwnd_ = nullptr;
  HICON icon_ = nullptr;
  bool closing_ = false;
  bool fullscreen_ = false;
  HCURSOR arrow_cursor_ = nullptr;

  WINDOWPLACEMENT windowed_pos_ = {0};
  POINT last_mouse_pos_ = {0};

  void* SetProcessDpiAwareness_ = nullptr;
  void* GetDpiForMonitor_ = nullptr;
};

class Win32MenuItem : public MenuItem {
 public:
  Win32MenuItem(Type type, const std::wstring& text, const std::wstring& hotkey,
                std::function<void()> callback);
  ~Win32MenuItem() override;

  HMENU handle() { return handle_; }

  void EnableMenuItem(Window& window) override;
  void DisableMenuItem(Window& window) override;

  using MenuItem::OnSelected;

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  HMENU handle_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_WIN_H_
