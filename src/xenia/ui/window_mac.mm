/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window_mac.h"

#import <Cocoa/Cocoa.h>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/surface_mac.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<MacWindow>(app_context, title, desired_logical_width,
                                     desired_logical_height);
}

std::unique_ptr<MenuItem> MenuItem::Create(Type type,
                                           const std::string& text,
                                           const std::string& hotkey,
                                           std::function<void()> callback) {
  return std::make_unique<MacMenuItem>(type, text, hotkey, std::move(callback));
}

}  // namespace ui
}  // namespace xe

// Objective-C delegate for handling window events
@interface XeniaWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) xe::ui::MacWindow* window;
- (instancetype)initWithWindow:(xe::ui::MacWindow*)window;
@end

@implementation XeniaWindowDelegate
- (instancetype)initWithWindow:(xe::ui::MacWindow*)window {
  self = [super init];
  if (self) {
    _window = window;
  }
  return self;
}

- (void)windowWillClose:(NSNotification*)notification {
  if (_window) {
    _window->OnWindowWillClose();
  }
}

- (void)windowDidResize:(NSNotification*)notification {
  if (_window) {
    _window->OnWindowDidResize();
  }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
  if (_window) {
    _window->OnWindowDidBecomeKey();
  }
}

- (void)windowDidResignKey:(NSNotification*)notification {
  if (_window) {
    _window->OnWindowDidResignKey();
  }
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  if (_window) {
    // Handle the close request with OnBeforeClose, then allow the window to close
    _window->RequestCloseFromDelegate();
    return YES;  // Allow the window to close after our handling
  }
  return YES;
}
@end

namespace xe {
namespace ui {

MacWindow::MacWindow(WindowedAppContext& app_context,
                     const std::string_view title,
                     uint32_t desired_logical_width,
                     uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width,
             desired_logical_height) {}

MacWindow::~MacWindow() {
  EnterDestructor();
  
  if (delegate_) {
    [delegate_ release];
    delegate_ = nullptr;
  }
  if (window_) {
    [window_ release];
    window_ = nullptr;
  }
}

bool MacWindow::OpenImpl() {
  assert_true(app_context().IsInUIThread());

  if (window_) {
    return true;
  }

  // Create the window
  NSRect frame = NSMakeRect(100, 100, GetDesiredLogicalWidth(),
                           GetDesiredLogicalHeight());
  
  window_ = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled |
                          NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];

  if (!window_) {
    XELOGE("Failed to create NSWindow");
    return false;
  }

  // Set up the window delegate
  delegate_ = [[XeniaWindowDelegate alloc] initWithWindow:this];
  [window_ setDelegate:delegate_];

  // Configure the window
  [window_ setTitle:[NSString stringWithUTF8String:GetTitle().c_str()]];
  [window_ setAcceptsMouseMovedEvents:YES];
  [window_ makeKeyAndOrderFront:nil];

  // Get the content view
  content_view_ = [window_ contentView];

  XELOGI("Created macOS window: {}x{}", 
         static_cast<int>(frame.size.width),
         static_cast<int>(frame.size.height));

  return true;
}

void MacWindow::RequestCloseFromDelegate() {
  // Handle close request from windowShouldClose delegate  
  if (is_closing_) {
    return;  // Already in close process
  }
  
  is_closing_ = true;
  
  // Call OnBeforeClose as per the pattern from GTK/Win32 implementations
  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  // Note: Don't call [window_ close] here - windowShouldClose will return YES
  // and the system will handle the actual window destruction
}

void MacWindow::RequestCloseImpl() {
  // This is called from the base Window class RequestClose()
  // Delegate to our close handler
  RequestCloseFromDelegate();
}

void MacWindow::ApplyNewFullscreen() {
  assert_true(app_context().IsInUIThread());
  
  if (!window_) {
    return;
  }

  bool is_fullscreen = ([window_ styleMask] & NSWindowStyleMaskFullScreen) != 0;
  
  if (IsFullscreen() != is_fullscreen) {
    [window_ toggleFullScreen:nil];
  }
}

void MacWindow::ApplyNewTitle() {
  assert_true(app_context().IsInUIThread());
  
  if (!window_) {
    return;
  }

  [window_ setTitle:[NSString stringWithUTF8String:GetTitle().c_str()]];
}

void MacWindow::ApplyNewMainMenu(MenuItem* old_main_menu) {
  assert_true(app_context().IsInUIThread());
  
  // TODO: Implement menu support
  XELOGE("MacWindow: Menu support not yet implemented");
}

void MacWindow::FocusImpl() {
  assert_true(app_context().IsInUIThread());
  
  if (window_) {
    [window_ makeKeyAndOrderFront:nil];
  }
}

std::unique_ptr<Surface> MacWindow::CreateSurfaceImpl(
    Surface::TypeFlags allowed_types) {
  assert_true(app_context().IsInUIThread());

  if (!content_view_) {
    return nullptr;
  }

  if (allowed_types & Surface::kTypeFlag_MacNSView) {
    return std::make_unique<MacNSViewSurface>(content_view_);
  }

  return nullptr;
}

void MacWindow::RequestPaintImpl() {
  assert_true(app_context().IsInUIThread());
  
  if (content_view_) {
    [content_view_ setNeedsDisplay:YES];
  }
}

void MacWindow::HandleSizeUpdate() {
  assert_true(app_context().IsInUIThread());
  
  if (!window_) {
    return;
  }

  NSRect frame = [window_ frame];
  NSRect content_rect = [window_ contentRectForFrameRect:frame];
  
  uint32_t width = static_cast<uint32_t>(content_rect.size.width);
  uint32_t height = static_cast<uint32_t>(content_rect.size.height);

  WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(width, height, destruction_receiver);
}

void MacWindow::OnWindowWillClose() {
  // The window is being closed by the system - handle final cleanup
  if (is_closing_) {
    // Clean up references - window is being destroyed by system
    window_ = nullptr;
    content_view_ = nullptr;
    
    // Complete the close handling
    OnAfterClose();
  }
}

void MacWindow::OnWindowDidResize() {
  HandleSizeUpdate();
}

void MacWindow::OnWindowDidBecomeKey() {
  WindowDestructionReceiver destruction_receiver(this);
  OnFocusUpdate(true, destruction_receiver);
}

void MacWindow::OnWindowDidResignKey() {
  WindowDestructionReceiver destruction_receiver(this);
  OnFocusUpdate(false, destruction_receiver);
}

// MacMenuItem implementation
MacMenuItem::MacMenuItem(Type type, const std::string& text,
                         const std::string& hotkey,
                         std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  // TODO: Implement NSMenuItem creation
}

MacMenuItem::~MacMenuItem() {
  // TODO: Implement NSMenuItem cleanup
}

void MacMenuItem::OnChildAdded(MenuItem* child_item) {
  // TODO: Implement submenu support
}

void MacMenuItem::OnChildRemoved(MenuItem* child_item) {
  // TODO: Implement submenu support
}

}  // namespace ui
}  // namespace xe
