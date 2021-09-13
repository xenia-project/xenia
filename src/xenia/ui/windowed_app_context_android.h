/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_

#include <android/native_activity.h>
#include <memory>

#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class AndroidWindow;
class WindowedApp;

class AndroidWindowedAppContext final : public WindowedAppContext {
 public:
  // For calling from android.app.func_name exports.
  static void StartAppOnNativeActivityCreate(
      ANativeActivity* activity, void* saved_state, size_t saved_state_size,
      std::unique_ptr<WindowedApp> (*app_creator)(
          WindowedAppContext& app_context));

  // Defined in the translation unit where WindowedApp is complete because of
  // std::unique_ptr.
  ~AndroidWindowedAppContext();

  ANativeActivity* activity() const { return activity_; }
  WindowedApp* app() const { return app_.get(); }

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  // The single Window instance that will be receiving window callbacks.
  // Multiple windows cannot be created as one activity or fragment can have
  // only one layout. This window acts purely as a proxy between the activity
  // and the Xenia logic.
  AndroidWindow* GetActivityWindow() const { return activity_window_; }
  void SetActivityWindow(AndroidWindow* window) { activity_window_ = window; }

 private:
  explicit AndroidWindowedAppContext(ANativeActivity* activity);
  bool InitializeApp(std::unique_ptr<WindowedApp> (*app_creator)(
      WindowedAppContext& app_context));

  // TODO(Triang3l): Switch from ANativeActivity to the context itself being the
  // object for communication with the Java code when NativeActivity isn't used
  // anymore as its functionality is heavily limited.
  ANativeActivity* activity_;
  std::unique_ptr<WindowedApp> app_;

  AndroidWindow* activity_window_ = nullptr;

  // TODO(Triang3l): The rest of the context, including quit handler (and the
  // destructor) calling `finish` on the activity, UI looper notification
  // posting, etc.
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
