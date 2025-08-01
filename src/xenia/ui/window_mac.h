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
@class NSEvent;
@class NSMenuItem;
#else
typedef struct objc_object NSWindow;
typedef struct objc_object NSView;
typedef struct objc_object XeniaWindowDelegate;
typedef struct objc_object NSEvent;
typedef struct objc_object NSMenuItem;
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
  
  // Public wrapper for delegate close requests - handle close without recursion
  void RequestCloseFromDelegate();

  // Handle mouse and keyboard events (public for XeniaContentView)
  void HandleMouseEvent(void* event);
  void HandleKeyEvent(void* event, bool is_down);

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
  
  // Helper method for menu creation
  NSMenuItem* CreateNSMenuItemFromMenuItem(MenuItem* menu_item);

  NSWindow* window_ = nullptr;
  NSView* content_view_ = nullptr;
  XeniaWindowDelegate* delegate_ = nullptr;
  bool is_closing_ = false;  // Flag to prevent recursive closing
  void* refresh_timer_ = nullptr;  // NSTimer* for continuous redraws

  friend class XeniaWindowDelegate;
};

class MacMenuItem : public MenuItem {
 public:
  MacMenuItem(Type type, const std::string& text, const std::string& hotkey,
              std::function<void()> callback);
  ~MacMenuItem() override;

  void* handle() const { return menu_item_; }
  
  // Public method to trigger menu selection from Objective-C callbacks
  void TriggerSelection() { OnSelected(); }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  void* menu_item_ = nullptr;  // NSMenuItem*
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_MAC_H_
