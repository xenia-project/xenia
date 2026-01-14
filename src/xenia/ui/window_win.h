/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_WIN_H_
#define XENIA_UI_WINDOW_WIN_H_

#include <memory>
#include <string>

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

// Must be included before Windows headers for things like NOMINMAX.
#include "xenia/base/platform_win.h"

#include "xenia/ui/dxgi_include_win.h"

#include <ShellScalingApi.h>

namespace xe {
namespace ui {

class Win32Window : public Window {
  using super = Window;

 public:
  Win32Window(WindowedAppContext& app_context, const std::string_view title,
              uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~Win32Window() override;

  // Will be null if the window hasn't been successfully opened yet, or has been
  // closed.
  HWND hwnd() const { return hwnd_; }

  uint32_t GetMediumDpi() const override;

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  uint32_t GetLatestDpiImpl() const override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void LoadAndApplyIcon(const void* buffer, size_t size,
                        bool can_apply_state_in_current_phase) override;
  void ApplyNewMainMenu(MenuItem* old_main_menu) override;
  void CompleteMainMenuItemsUpdateImpl() override;
  void ApplyNewMouseCapture() override;
  void ApplyNewMouseRelease() override;
  void ApplyNewCursorVisibility(
      CursorVisibility old_cursor_visibility) override;
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  enum : UINT {
    kUserMessageAutoHideCursor = WM_USER,
  };

  BOOL AdjustWindowRectangle(RECT& rect, DWORD style, BOOL menu, DWORD ex_style,
                             UINT dpi) const;
  BOOL AdjustWindowRectangle(RECT& rect) const;

  uint32_t GetCurrentSystemDpi() const;
  uint32_t GetCurrentDpi() const;

  void ApplyFullscreenEntry(WindowDestructionReceiver& destruction_receiver);

  void HandleSizeUpdate(WindowDestructionReceiver& destruction_receiver);
  // For updating multiple factors that may influence the window size at once,
  // without handling WM_SIZE multiple times (that may not only result in wasted
  // handling, but also in the state potentially changed to an inconsistent one
  // in the middle of a size update by the listeners).
  void BeginBatchedSizeUpdate();
  void EndBatchedSizeUpdate(WindowDestructionReceiver& destruction_receiver);

  bool HandleMouse(UINT message, WPARAM wParam, LPARAM lParam,
                   WindowDestructionReceiver& destruction_receiver);
  bool HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam,
                      WindowDestructionReceiver& destruction_receiver);

  void SetCursorIfFocusedOnClientArea(HCURSOR cursor) const;
  void SetCursorAutoHideTimer();
  static void NTAPI AutoHideCursorTimerCallback(void* parameter,
                                                BOOLEAN timer_or_wait_fired);

  static LRESULT CALLBACK WndProcThunk(HWND hWnd, UINT message, WPARAM wParam,
                                       LPARAM lParam);
  // This can't handle messages sent during CreateWindow (hwnd_ still not
  // assigned to) or after nulling hwnd_ in closing / deleting.
  virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam,
                          LPARAM lParam);

  HCURSOR arrow_cursor_ = nullptr;

  HICON icon_ = nullptr;

  uint32_t dpi_ = USER_DEFAULT_SCREEN_DPI;

  // hwnd_ may be accessed by the cursor hiding timer callback from a separate
  // thread, but the timer can be active only with a valid window anyway.
  HWND hwnd_ = nullptr;

  uint32_t batched_size_update_depth_ = 0;
  bool batched_size_update_contained_wm_size_ = false;
  bool batched_size_update_contained_wm_paint_ = false;

  uint32_t pre_fullscreen_dpi_;
  WINDOWPLACEMENT pre_fullscreen_placement_;
  // The client area part of pre_fullscreen_placement_.rcNormalPosition, saved
  // in case something effecting the behavior of AdjustWindowRectEx for the
  // non-fullscreen state is changed mid-fullscreen (for instance, the menu is
  // toggled), so a new AdjustWindowRectExForDpi call for the old DPI, but with
  // the other state different than the old one, while exiting fullscreen, won't
  // cause anomalies like negative size.
  uint32_t pre_fullscreen_normal_client_width_;
  uint32_t pre_fullscreen_normal_client_height_;

  // Must be the screen position, not the client position, so it's possible to
  // immediately hide the cursor, for instance, when switching to fullscreen
  // (and thus changing the client area top-left corner, resulting in
  // WM_MOUSEMOVE being sent, which would instantly reveal the cursor because of
  // that relative position change).
  POINT cursor_auto_hide_last_screen_pos_ = {LONG_MAX, LONG_MAX};
  // Using a timer queue timer for hiding the cursor rather than WM_TIMER
  // because the latter is a very low-priority message which is never received
  // if WM_PAINT messages are sent continuously (invalidating the window right
  // after painting).
  HANDLE cursor_auto_hide_timer_ = nullptr;
  // Last hiding case numbers for skipping of obsolete cursor hiding messages
  // (both WM_MOUSEMOVE and the hiding message have been sent, for instance, and
  // WM_MOUSEMOVE hasn't been handled yet, or the cursor visibility state has
  // been changed). The queued index is read, and the signaled index is written,
  // by the timer callback, which is executed outside the message thread, so
  // before modifying the queued number, or reading the signaled number, in the
  // message thread, delete the timer (deleting the timer awaits cancels the
  // callback if it hasn't been invoked yet, or awaits it if it has). Use
  // equality comparison for safe rollover handling.
  WPARAM last_cursor_auto_hide_queued = 0;
  WPARAM last_cursor_auto_hide_signaled = 0;
  // Whether the cursor has been hidden after the expiration of the timer, and
  // hasn't been revealed yet.
  bool cursor_currently_auto_hidden_ = false;
};

class Win32MenuItem : public MenuItem {
 public:
  Win32MenuItem(Type type, const std::string& text, const std::string& hotkey,
                std::function<void()> callback);
  ~Win32MenuItem() override;

  HMENU handle() const { return handle_; }

  void SetEnabled(bool enabled) override;

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
