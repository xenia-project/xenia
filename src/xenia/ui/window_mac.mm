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
#include "xenia/ui/ui_event.h"

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

// Custom view for handling mouse and keyboard events
@interface XeniaContentView : NSView {
  xe::ui::MacWindow* xenia_window_;
}
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

@implementation XeniaContentView
- (instancetype)initWithWindow:(xe::ui::MacWindow*)window {
  self = [super init];
  if (self) {
    xenia_window_ = window;
    
    // Set up tracking area for mouse events
    NSTrackingArea* trackingArea = [[NSTrackingArea alloc] 
        initWithRect:[self bounds]
             options:(NSTrackingMouseEnteredAndExited | 
                     NSTrackingMouseMoved | 
                     NSTrackingActiveInKeyWindow)
               owner:self
            userInfo:nil];
    [self addTrackingArea:trackingArea];
  }
  return self;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  
  // Remove old tracking areas
  for (NSTrackingArea* area in [self trackingAreas]) {
    [self removeTrackingArea:area];
  }
  
  // Add new tracking area
  NSTrackingArea* trackingArea = [[NSTrackingArea alloc] 
      initWithRect:[self bounds]
           options:(NSTrackingMouseEnteredAndExited | 
                   NSTrackingMouseMoved | 
                   NSTrackingActiveInKeyWindow)
             owner:self
          userInfo:nil];
  [self addTrackingArea:trackingArea];
}

- (BOOL)acceptsFirstResponder {
  return YES;  // Accept keyboard focus
}

- (BOOL)becomeFirstResponder {
  return YES;
}

- (void)mouseDown:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)mouseUp:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)mouseMoved:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)mouseDragged:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)rightMouseDown:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)rightMouseUp:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

- (void)keyDown:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleKeyEvent(event, true);
  }
}

- (void)keyUp:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleKeyEvent(event, false);
  }
}

- (void)scrollWheel:(NSEvent*)event {
  if (xenia_window_) {
    xenia_window_->HandleMouseEvent(event);
  }
}

// Override drawRect to trigger painting
- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  
  if (xenia_window_) {
    // Trigger a paint when the view needs to be redrawn
    xenia_window_->RequestPaint();
  }
}
@end

// Helper class for menu actions
@interface XeniaMenuActionHandler : NSObject
- (void)menuItemSelected:(NSMenuItem*)sender;
@end

@implementation XeniaMenuActionHandler
- (void)menuItemSelected:(NSMenuItem*)sender {
  NSValue* menu_item_value = [sender representedObject];
  if (menu_item_value) {
    xe::ui::MenuItem* menu_item = static_cast<xe::ui::MenuItem*>([menu_item_value pointerValue]);
    if (menu_item) {
      // Cast to MacMenuItem to call public TriggerSelection method
      xe::ui::MacMenuItem* mac_menu_item = static_cast<xe::ui::MacMenuItem*>(menu_item);
      mac_menu_item->TriggerSelection();
    }
  }
}
@end

// Static menu action handler
static XeniaMenuActionHandler* g_menu_handler = nil;

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
  
  // Stop the refresh timer
  if (refresh_timer_) {
    [(__bridge NSTimer*)refresh_timer_ invalidate];
    refresh_timer_ = nullptr;
  }
  
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

  // Create and set our custom content view for input handling
  XeniaContentView* custom_view = [[XeniaContentView alloc] initWithWindow:this];
  [custom_view setFrame:[[window_ contentView] frame]];
  [window_ setContentView:custom_view];
  content_view_ = custom_view;
  
  // Make the content view the first responder to ensure it receives events
  [window_ makeFirstResponder:custom_view];
  
  // Make the custom view the first responder to receive input events
  [window_ makeFirstResponder:custom_view];

  // Create a timer for continuous redraws (needed for ImGui animations)
  // Use a block-based timer to avoid Objective-C selector issues
  MacWindow* weakSelf = this;
  NSTimer* timer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0  // 60 FPS
                                                   repeats:YES
                                                     block:^(NSTimer* timer) {
    if (weakSelf && weakSelf->content_view_) {
      [weakSelf->content_view_ setNeedsDisplay:YES];
      // Also directly trigger OnPaint to ensure ImGui gets updated
      weakSelf->OnPaint();
    }
  }];
  refresh_timer_ = (__bridge void*)timer;

  // Apply the main menu if one was set before opening (similar to Windows implementation)
  const MacMenuItem* main_menu = static_cast<const MacMenuItem*>(GetMainMenu());
  if (main_menu) {
    ApplyNewMainMenu(nullptr);
  }

  // Report the initial size to the common Window class (like GTK does)
  // This is critical for ImGui and Vulkan swapchain initialization
  {
    WindowDestructionReceiver destruction_receiver(this);
    HandleSizeUpdate();
    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return false;
    }
  }

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
  
  auto main_menu = GetMainMenu();
  if (!main_menu) {
    // Clear the menu if none provided
    [NSApp setMainMenu:nil];
    return;
  }
  
  // Create the main menu bar
  NSMenu* menu_bar = [[NSMenu alloc] initWithTitle:@"MainMenu"];
  
  // Add each top-level menu item
  for (size_t i = 0; i < main_menu->child_count(); ++i) {
    MenuItem* child = main_menu->child(i);
    NSMenuItem* menu_item = CreateNSMenuItemFromMenuItem(child);
    if (menu_item) {
      [menu_bar addItem:menu_item];
    }
  }
  
  // Set as the application's main menu
  [NSApp setMainMenu:menu_bar];
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
  
  // Immediately trigger OnPaint since we don't have drawRect: yet
  // This ensures ImGui and other drawing calls happen regularly
  OnPaint();
}

void MacWindow::HandleSizeUpdate() {
  assert_true(app_context().IsInUIThread());
  
  if (!window_) {
    return;
  }

  NSRect frame = [window_ frame];
  NSRect content_rect = [window_ contentRectForFrameRect:frame];
  
  // Report logical size like GTK does - let the base class handle DPI scaling
  uint32_t logical_width = static_cast<uint32_t>(content_rect.size.width);
  uint32_t logical_height = static_cast<uint32_t>(content_rect.size.height);
  
  WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(logical_width, logical_height, destruction_receiver);
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

void MacWindow::HandleMouseEvent(void* event) {
  assert_true(app_context().IsInUIThread());
  
  NSEvent* ns_event = (__bridge NSEvent*)event;
  NSPoint window_location = [ns_event locationInWindow];
  
  // Convert from window coordinates to view coordinates
  NSPoint view_location = [content_view_ convertPoint:window_location fromView:nil];
  
  // Convert from Cocoa's bottom-left origin to top-left origin
  NSRect view_bounds = [content_view_ bounds];
  int32_t x = static_cast<int32_t>(view_location.x);
  int32_t y = static_cast<int32_t>(view_bounds.size.height - view_location.y);
  
  // Clamp coordinates to view bounds
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= view_bounds.size.width) x = static_cast<int32_t>(view_bounds.size.width - 1);
  if (y >= view_bounds.size.height) y = static_cast<int32_t>(view_bounds.size.height - 1);
  
  float scroll_x = 0.0f;
  float scroll_y = 0.0f;
  
  if ([ns_event type] == NSEventTypeScrollWheel) {
    scroll_x = static_cast<float>([ns_event scrollingDeltaX]);
    scroll_y = static_cast<float>([ns_event scrollingDeltaY]);
  }
  
  MouseEvent::Button button = MouseEvent::Button::kNone;
  NSEventType event_type = [ns_event type];
  
  switch (event_type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
      button = MouseEvent::Button::kLeft;
      break;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
      button = MouseEvent::Button::kRight;
      break;
    default:
      break;
  }
  
  MouseEvent e(this, button, x, y, scroll_x, scroll_y);
  WindowDestructionReceiver destruction_receiver(this);
  
  switch (event_type) {
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
      OnMouseMove(e, destruction_receiver);
      break;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
      OnMouseDown(e, destruction_receiver);
      break;
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
      OnMouseUp(e, destruction_receiver);
      break;
    case NSEventTypeScrollWheel:
      OnMouseWheel(e, destruction_receiver);
      break;
    default:
      break;
  }
}

void MacWindow::HandleKeyEvent(void* event, bool is_down) {
  assert_true(app_context().IsInUIThread());
  
  NSEvent* ns_event = (__bridge NSEvent*)event;
  NSEventModifierFlags modifiers = [ns_event modifierFlags];
  
  bool shift_pressed = (modifiers & NSEventModifierFlagShift) != 0;
  bool ctrl_pressed = (modifiers & NSEventModifierFlagControl) != 0;
  bool alt_pressed = (modifiers & NSEventModifierFlagOption) != 0;
  bool super_pressed = (modifiers & NSEventModifierFlagCommand) != 0;
  
  // Convert NSEvent keyCode to our VirtualKey
  // Map macOS keyCodes to Windows Virtual Key codes
  VirtualKey virtual_key;
  uint16_t key_code = [ns_event keyCode];
  
  switch (key_code) {
    case 122: virtual_key = VirtualKey::kF1; break;   // F1
    case 120: virtual_key = VirtualKey::kF2; break;   // F2  
    case 99:  virtual_key = VirtualKey::kF3; break;   // F3
    case 118: virtual_key = VirtualKey::kF4; break;   // F4
    case 96:  virtual_key = VirtualKey::kF5; break;   // F5
    case 97:  virtual_key = VirtualKey::kF6; break;   // F6
    case 98:  virtual_key = VirtualKey::kF7; break;   // F7
    case 100: virtual_key = VirtualKey::kF8; break;   // F8
    case 101: virtual_key = VirtualKey::kF9; break;   // F9
    case 109: virtual_key = VirtualKey::kF10; break;  // F10
    case 103: virtual_key = VirtualKey::kF11; break;  // F11
    case 111: virtual_key = VirtualKey::kF12; break;  // F12
    case 50:  virtual_key = VirtualKey::kOem3; break; // ` (backtick)
    // Letter keys
    case 0:   virtual_key = VirtualKey::kA; break;    // A
    case 11:  virtual_key = VirtualKey::kB; break;    // B
    case 8:   virtual_key = VirtualKey::kC; break;    // C
    case 2:   virtual_key = VirtualKey::kD; break;    // D
    case 14:  virtual_key = VirtualKey::kE; break;    // E
    case 3:   virtual_key = VirtualKey::kF; break;    // F
    case 5:   virtual_key = VirtualKey::kG; break;    // G
    case 4:   virtual_key = VirtualKey::kH; break;    // H
    case 34:  virtual_key = VirtualKey::kI; break;    // I
    case 38:  virtual_key = VirtualKey::kJ; break;    // J
    case 40:  virtual_key = VirtualKey::kK; break;    // K
    case 37:  virtual_key = VirtualKey::kL; break;    // L
    case 46:  virtual_key = VirtualKey::kM; break;    // M
    case 45:  virtual_key = VirtualKey::kN; break;    // N
    case 31:  virtual_key = VirtualKey::kO; break;    // O
    case 35:  virtual_key = VirtualKey::kP; break;    // P
    case 12:  virtual_key = VirtualKey::kQ; break;    // Q
    case 15:  virtual_key = VirtualKey::kR; break;    // R
    case 1:   virtual_key = VirtualKey::kS; break;    // S
    case 17:  virtual_key = VirtualKey::kT; break;    // T
    case 32:  virtual_key = VirtualKey::kU; break;    // U
    case 9:   virtual_key = VirtualKey::kV; break;    // V
    case 13:  virtual_key = VirtualKey::kW; break;    // W
    case 7:   virtual_key = VirtualKey::kX; break;    // X
    case 16:  virtual_key = VirtualKey::kY; break;    // Y
    case 6:   virtual_key = VirtualKey::kZ; break;    // Z
    default:
      // For other keys, just cast the keycode (this is imperfect but works for many keys)
      virtual_key = VirtualKey(key_code);
      break;
  }
  
  KeyEvent e(this, virtual_key, 1, !is_down, shift_pressed, ctrl_pressed,
             alt_pressed, super_pressed);
  WindowDestructionReceiver destruction_receiver(this);
  
  if (is_down) {
    OnKeyDown(e, destruction_receiver);
  } else {
    OnKeyUp(e, destruction_receiver);
  }
}

NSMenuItem* MacWindow::CreateNSMenuItemFromMenuItem(MenuItem* menu_item) {
  if (!menu_item) {
    return nil;
  }
  
  // Initialize the menu handler if needed
  if (!g_menu_handler) {
    g_menu_handler = [[XeniaMenuActionHandler alloc] init];
  }
  
  NSString* title = [NSString stringWithUTF8String:menu_item->text().c_str()];
  
  // Remove & accelerator markers for macOS (they're used differently)
  title = [title stringByReplacingOccurrencesOfString:@"&" withString:@""];
  
  NSMenuItem* ns_item = nil;
  
  switch (menu_item->type()) {
    case MenuItem::Type::kNormal:
      // This is a container - create empty item
      ns_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
      break;
      
    case MenuItem::Type::kPopup: {
      // This is a submenu
      ns_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
      NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
      
      // Add all children to the submenu
      for (size_t i = 0; i < menu_item->child_count(); ++i) {
        MenuItem* child = menu_item->child(i);
        NSMenuItem* child_item = CreateNSMenuItemFromMenuItem(child);
        if (child_item) {
          [submenu addItem:child_item];
        }
      }
      
      [ns_item setSubmenu:submenu];
      break;
    }
    
    case MenuItem::Type::kString: {
      // Regular menu item with action
      NSString* key_equivalent = @"";
      NSEventModifierFlags modifier_flags = 0;
      
      // Parse hotkey - handle Command+key shortcuts properly
      if (menu_item->hotkey() == "Cmd+G") {
        key_equivalent = @"g";
        modifier_flags = NSEventModifierFlagCommand;
      } else if (menu_item->hotkey() == "Cmd+B") {
        key_equivalent = @"b";
        modifier_flags = NSEventModifierFlagCommand;
      }
      
      ns_item = [[NSMenuItem alloc] initWithTitle:title 
                                           action:@selector(menuItemSelected:) 
                                    keyEquivalent:key_equivalent];
                                    
      if (modifier_flags != 0) {
        [ns_item setKeyEquivalentModifierMask:modifier_flags];
      }
      
      // Store the MenuItem pointer for callback
      NSValue* menu_item_value = [NSValue valueWithPointer:menu_item];
      [ns_item setRepresentedObject:menu_item_value];
      
      [ns_item setTarget:g_menu_handler];
      break;
    }
    
    default:
      break;
  }
  
  return ns_item;
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
