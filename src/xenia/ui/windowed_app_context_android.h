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

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <jni.h>
#include <array>
#include <memory>

#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace ui {

class AndroidWindow;
class WindowedApp;

class AndroidWindowedAppContext final : public WindowedAppContext {
 public:
  WindowedApp* app() const { return app_.get(); }

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  // The single Window instance that will be receiving window callbacks.
  // Multiple windows cannot be created as one activity or fragment can have
  // only one layout. This window acts purely as a proxy between the activity
  // and the Xenia logic.
  AndroidWindow* GetActivityWindow() const { return activity_window_; }
  void SetActivityWindow(AndroidWindow* window) { activity_window_ = window; }

  // For calling from WindowedAppActivity native methods.
  static AndroidWindowedAppContext* JniActivityInitializeWindowedAppOnCreate(
      JNIEnv* jni_env, jobject activity, jstring windowed_app_identifier,
      jobject asset_manager);
  void JniActivityOnDestroy();

 private:
  enum class UIThreadLooperCallbackCommand : uint8_t {
    kDestroy,
    kExecutePendingFunctions,
  };

  AndroidWindowedAppContext() = default;

  // Don't delete this object directly externally if successfully initialized as
  // the looper may still execute the callback for pending commands after an
  // external ALooper_removeFd, and the callback receives a pointer to the
  // context - deletion must be deferred and done in the callback itself.
  // Defined in the translation unit where WindowedApp is complete because of
  // std::unique_ptr.
  ~AndroidWindowedAppContext();

  bool Initialize(JNIEnv* ui_thread_jni_env, jobject activity,
                  jobject asset_manager);
  void Shutdown();

  // Call this function instead of deleting the object directly, so if needed,
  // deletion will be deferred until the callback (receiving a pointer to the
  // context) can no longer be executed by the looper (will be done inside the
  // callback).
  void RequestDestruction();

  static int UIThreadLooperCallback(int fd, int events, void* data);

  bool InitializeApp(std::unique_ptr<WindowedApp> (*app_creator)(
      WindowedAppContext& app_context));

  // Useful notes about JNI usage on Android within Xenia:
  // - All static libraries defining JNI native functions must be linked to
  //   shared libraries via LOCAL_WHOLE_STATIC_LIBRARIES.
  // - If method or field IDs are cached, a global reference to the class needs
  //   to be held - it prevents the class from being unloaded by the class
  //   loaders (in a way that would make the IDs invalid when it's reloaded).
  // - GetStringUTFChars (UTF-8) returns null-terminated strings, GetStringChars
  //   (UTF-16) does not.
  JNIEnv* ui_thread_jni_env_ = nullptr;

  // The object reference must be held by the app according to
  // AAssetManager_fromJava documentation.
  jobject asset_manager_jobject_ = nullptr;
  AAssetManager* asset_manager_ = nullptr;

  AConfiguration* configuration_ = nullptr;

  bool android_base_initialized_ = false;

  jobject activity_ = nullptr;
  jclass activity_class_ = nullptr;
  jmethodID activity_method_finish_ = nullptr;

  // May be read by non-UI threads in NotifyUILoopOfPendingFunctions.
  ALooper* ui_thread_looper_ = nullptr;
  // [1] (the write file descriptor) may be referenced as read-only by non-UI
  // threads in NotifyUILoopOfPendingFunctions.
  std::array<int, 2> ui_thread_looper_callback_pipe_{-1, -1};
  bool ui_thread_looper_callback_registered_ = false;

  AndroidWindow* activity_window_ = nullptr;

  std::unique_ptr<WindowedApp> app_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
