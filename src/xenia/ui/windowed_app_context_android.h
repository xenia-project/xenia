/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_window.h>
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
  // Precached JNI references and IDs that may be used for windowed app purposes
  // by external code.
  struct JniIDs {
    // android.view.MotionEvent.
    jmethodID motion_event_get_action;
    jmethodID motion_event_get_axis_value;
    jmethodID motion_event_get_button_state;
    jmethodID motion_event_get_pointer_count;
    jmethodID motion_event_get_pointer_id;
    jmethodID motion_event_get_source;
    jmethodID motion_event_get_x;
    jmethodID motion_event_get_y;
  };

  WindowedApp* app() const { return app_.get(); }

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  JNIEnv* ui_thread_jni_env() const { return ui_thread_jni_env_; }

  uint32_t GetPixelDensity() const {
    return configuration_ ? uint32_t(AConfiguration_getDensity(configuration_))
                          : 160;
  }

  const JniIDs& jni_ids() const { return jni_ids_; }

  int32_t window_surface_layout_left() const {
    return window_surface_layout_left_;
  }
  int32_t window_surface_layout_top() const {
    return window_surface_layout_top_;
  }
  int32_t window_surface_layout_right() const {
    return window_surface_layout_right_;
  }
  int32_t window_surface_layout_bottom() const {
    return window_surface_layout_bottom_;
  }

  ANativeWindow* GetWindowSurface() const { return window_surface_; }
  void PostInvalidateWindowSurface();

  // The single Window instance that will be receiving window callbacks.
  // Multiple windows cannot be created as one activity or fragment can have
  // only one layout. This window acts purely as a proxy between the activity
  // and the Xenia logic. May be done during Window destruction, so must not
  // interact with it.
  AndroidWindow* GetActivityWindow() const { return activity_window_; }
  void SetActivityWindow(AndroidWindow* window) { activity_window_ = window; }

  // For calling from WindowedAppActivity native methods.
  static AndroidWindowedAppContext* JniActivityInitializeWindowedAppOnCreate(
      JNIEnv* jni_env, jobject activity, jstring windowed_app_identifier,
      jobject asset_manager);
  void JniActivityOnDestroy();
  void JniActivityOnWindowSurfaceLayoutChange(jint left, jint top, jint right,
                                              jint bottom);
  bool JniActivityOnWindowSurfaceMotionEvent(jobject event);
  void JniActivityOnWindowSurfaceChanged(jobject window_surface_object);
  void JniActivityPaintWindow(bool force_paint);

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

  JNIEnv* ui_thread_jni_env_ = nullptr;

  // The object reference must be held by the app according to
  // AAssetManager_fromJava documentation.
  jobject asset_manager_jobject_ = nullptr;
  AAssetManager* asset_manager_ = nullptr;

  AConfiguration* configuration_ = nullptr;

  jclass activity_class_ = nullptr;

  bool android_base_initialized_ = false;

  jclass motion_event_class_ = nullptr;
  JniIDs jni_ids_ = {};

  jobject activity_ = nullptr;
  jmethodID activity_method_finish_ = nullptr;
  jmethodID activity_method_post_invalidate_window_surface_ = nullptr;

  // May be read by non-UI threads in NotifyUILoopOfPendingFunctions.
  ALooper* ui_thread_looper_ = nullptr;
  // [1] (the write file descriptor) may be referenced as read-only by non-UI
  // threads in NotifyUILoopOfPendingFunctions.
  std::array<int, 2> ui_thread_looper_callback_pipe_{-1, -1};
  bool ui_thread_looper_callback_registered_ = false;

  int32_t window_surface_layout_left_ = 0;
  int32_t window_surface_layout_top_ = 0;
  int32_t window_surface_layout_right_ = 0;
  int32_t window_surface_layout_bottom_ = 0;

  ANativeWindow* window_surface_ = nullptr;

  AndroidWindow* activity_window_ = nullptr;

  std::unique_ptr<WindowedApp> app_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_ANDROID_H_
