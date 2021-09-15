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

#include <android/looper.h>
#include <android/native_activity.h>
#include <array>
#include <memory>

#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class AndroidWindow;
class WindowedApp;

class AndroidWindowedAppContext final : public WindowedAppContext {
 public:
  // For calling from android.app.func_name exports.
  static void StartAppOnActivityCreate(
      ANativeActivity* activity, void* saved_state, size_t saved_state_size,
      std::unique_ptr<WindowedApp> (*app_creator)(
          WindowedAppContext& app_context));

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
  enum class UIThreadLooperCallbackCommand : uint8_t {
    kDestroy,
    kExecutePendingFunctions,
  };

  AndroidWindowedAppContext() = default;

  // Don't delete this object directly externally if successfully initialized as
  // the looper may still execute the callback for pending commands after an
  // external ANativeActivity_removeFd, and the callback receives a pointer to
  // the context - deletion must be deferred and done in the callback itself.
  // Defined in the translation unit where WindowedApp is complete because of
  // std::unique_ptr.
  ~AndroidWindowedAppContext();

  bool Initialize(ANativeActivity* activity);
  void Shutdown();

  // Call this function instead of deleting the object directly, so if needed,
  // deletion will be deferred until the callback (receiving a pointer to the
  // context) can no longer be executed by the looper (will be done inside the
  // callback).
  void RequestDestruction();

  static int UIThreadLooperCallback(int fd, int events, void* data);

  bool InitializeApp(std::unique_ptr<WindowedApp> (*app_creator)(
      WindowedAppContext& app_context));

  static void OnActivityDestroy(ANativeActivity* activity);

  bool android_base_initialized_ = false;

  // May be read by non-UI threads in NotifyUILoopOfPendingFunctions.
  ALooper* ui_thread_looper_ = nullptr;
  // [1] (the write file descriptor) may be referenced as read-only by non-UI
  // threads in NotifyUILoopOfPendingFunctions.
  std::array<int, 2> ui_thread_looper_callback_pipe_{-1, -1};
  bool ui_thread_looper_callback_registered_ = false;

  // TODO(Triang3l): Switch from ANativeActivity to the context itself being the
  // object for communication with the Java code when NativeActivity isn't used
  // anymore as its functionality is heavily limited.
  ANativeActivity* activity_ = nullptr;

  AndroidWindow* activity_window_ = nullptr;

  std::unique_ptr<WindowedApp> app_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
