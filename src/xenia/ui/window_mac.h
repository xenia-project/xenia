/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_MAC_H_
#define XENIA_UI_WINDOW_MAC_H_

#include <memory>
#include <string>

#include "xenia/ui/menu_item.h"
#include "xenia/ui/window.h"

#ifdef __OBJC__
@class NSWindow;
@class NSView;
@class XeniaWindowDelegate;
#else
typedef struct objc_object NSWindow;
typedef struct objc_object NSView;
typedef struct objc_object XeniaWindowDelegate;
#endif

namespace xe {
namespace ui {

class MacWindow : public Window {
  using super = Window;

 public:
  MacWindow(WindowedAppContext& app_context, const std::string_view title,
            uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~MacWindow() override;

  // Will be null if the window hasn't been successfully opened yet, or has been
  // closed.
  NSWindow* window() const { return window_; }
  NSView* content_view() const { return content_view_; }

  // Event handlers called from Objective-C delegate (must be public)
  void OnWindowWillClose();
  void OnWindowDidResize();
  void OnWindowDidBecomeKey();
  void OnWindowDidResignKey();
  
  // Public wrapper for delegate close requests
  void RequestCloseFromDelegate() { RequestCloseImpl(); }

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;

  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void ApplyNewMainMenu(MenuItem* old_main_menu) override;
  void FocusImpl() override;

  std::unique_ptr<Surface> CreateSurfaceImpl(
      Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  void HandleSizeUpdate();
  
  // Handle mouse and keyboard events
  void HandleMouseEvent(void* event);
  void HandleKeyEvent(void* event, bool is_down);

  NSWindow* window_ = nullptr;
  NSView* content_view_ = nullptr;
  XeniaWindowDelegate* delegate_ = nullptr;

  friend class XeniaWindowDelegate;
};

class MacMenuItem : public MenuItem {
 public:
  MacMenuItem(Type type, const std::string& text, const std::string& hotkey,
              std::function<void()> callback);
  ~MacMenuItem() override;

  void* handle() const { return menu_item_; }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  void* menu_item_ = nullptr;  // NSMenuItem*
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_MAC_H_
