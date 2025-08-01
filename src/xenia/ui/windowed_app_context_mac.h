/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_

#include <mutex>

#include "xenia/ui/windowed_app_context.h"

#ifdef __OBJC__
@class NSApplication;
@class XeniaAppDelegate;
#else
typedef struct objc_object NSApplication;
typedef struct objc_object XeniaAppDelegate;
#endif

namespace xe {
namespace ui {

class MacWindowedAppContext final : public WindowedAppContext {
 public:
  MacWindowedAppContext();
  ~MacWindowedAppContext();

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  void RunMainCocoaLoop();

 private:
  void ProcessPendingFunctions();
  
  NSApplication* app_ = nullptr;
  XeniaAppDelegate* delegate_ = nullptr;
  
  std::mutex pending_functions_mutex_;
  bool pending_functions_scheduled_ = false;
  bool should_quit_ = false;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_MAC_H_
