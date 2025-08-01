/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_mac.h"

#import <Cocoa/Cocoa.h>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

// Application delegate for handling app events
@interface XeniaAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) xe::ui::MacWindowedAppContext* context;
- (instancetype)initWithContext:(xe::ui::MacWindowedAppContext*)context;
@end

@implementation XeniaAppDelegate
- (instancetype)initWithContext:(xe::ui::MacWindowedAppContext*)context {
  self = [super init];
  if (self) {
    _context = context;
  }
  return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  // App has finished launching
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  if (_context) {
    _context->PlatformQuitFromUIThread();
  }
  return NSTerminateCancel;  // Let our quit handling manage termination
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return YES;
}
@end

namespace xe {
namespace ui {

MacWindowedAppContext::MacWindowedAppContext() {
  // Initialize the NSApplication
  app_ = [NSApplication sharedApplication];
  if (!app_) {
    XELOGE("Failed to get NSApplication shared instance");
    return;
  }

  // Set up the app delegate
  delegate_ = [[XeniaAppDelegate alloc] initWithContext:this];
  [app_ setDelegate:delegate_];

  // Set the activation policy to make the app behave like a regular app
  [app_ setActivationPolicy:NSApplicationActivationPolicyRegular];

  XELOGI("Initialized macOS app context");
}

MacWindowedAppContext::~MacWindowedAppContext() {
  if (delegate_) {
    [delegate_ release];
    delegate_ = nullptr;
  }
}

void MacWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  std::lock_guard<std::mutex> lock(pending_functions_mutex_);
  
  if (pending_functions_scheduled_) {
    return;
  }
  
  pending_functions_scheduled_ = true;
  
  // Schedule on the main queue to process pending functions
  dispatch_async(dispatch_get_main_queue(), ^{
    ProcessPendingFunctions();
  });
}

void MacWindowedAppContext::PlatformQuitFromUIThread() {
  assert_true(IsInUIThread());
  
  should_quit_ = true;
  [app_ stop:nil];
  
  // Post a dummy event to wake up the event loop
  NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSMakePoint(0, 0)
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
  [app_ postEvent:event atStart:YES];
}

void MacWindowedAppContext::RunMainCocoaLoop() {
  assert_true(IsInUIThread());
  
  if (HasQuitFromUIThread()) {
    return;
  }

  XELOGI("Starting Cocoa main loop");
  
  // Make the app active and bring to front
  [app_ activateIgnoringOtherApps:YES];
  
  // Run the main event loop
  [app_ run];
  
  // If we exit the loop for any reason, mark as quit
  if (!HasQuitFromUIThread()) {
    QuitFromUIThread();
  }
  
  XELOGI("Cocoa main loop ended");
}

void MacWindowedAppContext::ProcessPendingFunctions() {
  {
    std::lock_guard<std::mutex> lock(pending_functions_mutex_);
    pending_functions_scheduled_ = false;
  }
  
  // Execute all pending functions
  ExecutePendingFunctionsFromUIThread();
}

}  // namespace ui
}  // namespace xe
