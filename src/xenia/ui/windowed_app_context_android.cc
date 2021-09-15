/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_android.h"

#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/main_android.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

void AndroidWindowedAppContext::StartAppOnActivityCreate(
    ANativeActivity* activity, [[maybe_unused]] void* saved_state,
    [[maybe_unused]] size_t saved_state_size,
    std::unique_ptr<WindowedApp> (*app_creator)(
        WindowedAppContext& app_context)) {
  // TODO(Triang3l): Pass the launch options from the Intent or the saved
  // instance state.
  AndroidWindowedAppContext* app_context = new AndroidWindowedAppContext;
  if (!app_context->Initialize(activity)) {
    delete app_context;
    ANativeActivity_finish(activity);
    return;
  }
  // The pointer is now held by the Activity as its ANativeActivity::instance,
  // until the destruction.
  if (!app_context->InitializeApp(app_creator)) {
    // InitializeApp might have sent commands to the UI thread looper callback
    // pipe, perform deferred destruction.
    app_context->RequestDestruction();
    ANativeActivity_finish(activity);
    return;
  }
}

void AndroidWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  // Don't check ui_thread_looper_callback_registered_, as it's owned
  // exclusively by the UI thread, while this may be called by any, and in case
  // of a pipe error, the callback will be invoked by the looper, which will
  // trigger all the necessary shutdown, and the pending functions will be
  // called anyway by the shutdown.
  UIThreadLooperCallbackCommand command =
      UIThreadLooperCallbackCommand::kExecutePendingFunctions;
  if (write(ui_thread_looper_callback_pipe_[1], &command, sizeof(command)) !=
      sizeof(command)) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to write a pending function "
        "execution command to the UI thread looper callback pipe");
    return;
  }
  ALooper_wake(ui_thread_looper_);
}

void AndroidWindowedAppContext::PlatformQuitFromUIThread() {
  // All the shutdown will be done in onDestroy of the activity.
  ANativeActivity_finish(activity_);
}

AndroidWindowedAppContext::~AndroidWindowedAppContext() { Shutdown(); }

bool AndroidWindowedAppContext::Initialize(ANativeActivity* activity) {
  int32_t api_level;
  {
    AConfiguration* configuration = AConfiguration_new();
    AConfiguration_fromAssetManager(configuration, activity->assetManager);
    api_level = AConfiguration_getSdkVersion(configuration);
    AConfiguration_delete(configuration);
  }
  xe::InitializeAndroidAppFromMainThread(api_level);
  android_base_initialized_ = true;

  // Initialize sending commands to the UI thread looper callback, for
  // requesting function calls in the UI thread.
  ui_thread_looper_ = ALooper_forThread();
  // The context may be created only in the UI thread, which must have an
  // internal looper.
  assert_not_null(ui_thread_looper_);
  if (!ui_thread_looper_) {
    XELOGE("AndroidWindowedAppContext: Failed to get the UI thread looper");
    Shutdown();
    return false;
  }
  // The looper can be woken up by other threads, so acquiring it. Shutdown
  // assumes that if ui_thread_looper_ is not null, it has been acquired.
  ALooper_acquire(ui_thread_looper_);
  if (pipe(ui_thread_looper_callback_pipe_.data())) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to create the UI thread looper "
        "callback pipe");
    Shutdown();
    return false;
  }
  if (ALooper_addFd(ui_thread_looper_, ui_thread_looper_callback_pipe_[0],
                    ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                    UIThreadLooperCallback, this) != 1) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to add the callback to the UI "
        "thread looper");
    Shutdown();
    return false;
  }
  ui_thread_looper_callback_registered_ = true;

  activity_ = activity;
  activity_->instance = this;
  activity_->callbacks->onDestroy = OnActivityDestroy;

  return true;
}

void AndroidWindowedAppContext::Shutdown() {
  if (app_) {
    app_->InvokeOnDestroy();
    app_.reset();
  }

  // The app should destroy the window, but make sure everything is cleaned up
  // anyway.
  assert_null(activity_window_);
  activity_window_ = nullptr;

  if (activity_) {
    activity_->callbacks->onDestroy = nullptr;
    activity_->instance = nullptr;
    activity_ = nullptr;
  }

  if (ui_thread_looper_callback_registered_) {
    ALooper_removeFd(ui_thread_looper_, ui_thread_looper_callback_pipe_[0]);
    ui_thread_looper_callback_registered_ = false;
  }
  for (int& pipe_fd : ui_thread_looper_callback_pipe_) {
    if (pipe_fd == -1) {
      continue;
    }
    close(pipe_fd);
    pipe_fd = -1;
  }
  if (ui_thread_looper_) {
    ALooper_release(ui_thread_looper_);
    ui_thread_looper_ = nullptr;
  }

  if (android_base_initialized_) {
    xe::ShutdownAndroidAppFromMainThread();
    android_base_initialized_ = false;
  }
}

void AndroidWindowedAppContext::RequestDestruction() {
  // According to ALooper_removeFd documentation:
  // "...it is possible for the callback to already be running or for it to run
  //  one last time if the file descriptor was already signalled. Calling code
  //  is responsible for ensuring that this case is safely handled. For example,
  //  if the callback takes care of removing itself during its own execution
  //  either by returning 0 or by calling this method..."
  // If the looper callback is registered, the pipe may have pending commands,
  // and thus the callback may still be called with the pointer to the context
  // as the user data.
  if (!ui_thread_looper_callback_registered_) {
    delete this;
    return;
  }
  UIThreadLooperCallbackCommand command =
      UIThreadLooperCallbackCommand::kDestroy;
  if (write(ui_thread_looper_callback_pipe_[1], &command, sizeof(command)) !=
      sizeof(command)) {
    XELOGE(
        "AndroidWindowedAppContext: Failed to write a destruction command to "
        "the UI thread looper callback pipe");
    delete this;
    return;
  }
  ALooper_wake(ui_thread_looper_);
}

int AndroidWindowedAppContext::UIThreadLooperCallback(int fd, int events,
                                                      void* data) {
  // In case of errors, destruction of the pipe (most importantly the write end)
  // must not be done here immediately as other threads, which may still be
  // sending commands, would not be aware of that.
  auto app_context = static_cast<AndroidWindowedAppContext*>(data);
  if (events &
      (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP | ALOOPER_EVENT_INVALID)) {
    // Will return 0 to unregister self, this file descriptor is not usable
    // anymore, so let everything potentially referencing it in QuitFromUIThread
    // know.
    app_context->ui_thread_looper_callback_registered_ = false;
    XELOGE(
        "AndroidWindowedAppContext: The UI thread looper callback pipe file "
        "descriptor has encountered an error condition during polling");
    app_context->QuitFromUIThread();
    return 0;
  }
  if (!(events & ALOOPER_EVENT_INPUT)) {
    // Spurious callback call. Need a non-empty pipe.
    return 1;
  }
  // Process one command with a blocking `read`. The callback will be invoked
  // again and again if there is still data after this read.
  UIThreadLooperCallbackCommand command;
  switch (read(fd, &command, sizeof(command))) {
    case sizeof(command):
      break;
    case -1:
      // Will return 0 to unregister self, this file descriptor is not usable
      // anymore, so let everything potentially referencing it in
      // QuitFromUIThread know.
      app_context->ui_thread_looper_callback_registered_ = false;
      XELOGE(
          "AndroidWindowedAppContext: The UI thread looper callback pipe file "
          "descriptor has encountered an error condition during reading");
      app_context->QuitFromUIThread();
      return 0;
    default:
      // Something like incomplete data - shouldn't be happening, but not a
      // reported error.
      return 1;
  }
  switch (command) {
    case UIThreadLooperCallbackCommand::kDestroy:
      // Final destruction requested. Will unregister self by returning 0, so
      // set ui_thread_looper_callback_registered_ to false so Shutdown won't
      // try to unregister it too.
      app_context->ui_thread_looper_callback_registered_ = false;
      delete app_context;
      return 0;
    case UIThreadLooperCallbackCommand::kExecutePendingFunctions:
      app_context->ExecutePendingFunctionsFromUIThread();
      break;
  }
  return 1;
}

bool AndroidWindowedAppContext::InitializeApp(std::unique_ptr<WindowedApp> (
    *app_creator)(WindowedAppContext& app_context)) {
  assert_null(app_);
  app_ = app_creator(*this);
  if (!app_->OnInitialize()) {
    app_->InvokeOnDestroy();
    app_.reset();
    return false;
  }
  return true;
}

void AndroidWindowedAppContext::OnActivityDestroy(ANativeActivity* activity) {
  auto& app_context =
      *static_cast<AndroidWindowedAppContext*>(activity->instance);
  if (app_context.app_) {
    app_context.app_->InvokeOnDestroy();
    app_context.app_.reset();
  }
  app_context.RequestDestruction();
}

}  // namespace ui
}  // namespace xe
