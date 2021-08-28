/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context_android.h"

#include <android/native_activity.h>

#include "xenia/base/assert.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

void AndroidWindowedAppContext::StartAppOnNativeActivityCreate(
    ANativeActivity* activity, [[maybe_unused]] void* saved_state,
    [[maybe_unused]] size_t saved_state_size,
    std::unique_ptr<WindowedApp> (*app_creator)(
        WindowedAppContext& app_context)) {
  AndroidWindowedAppContext* app_context =
      new AndroidWindowedAppContext(activity);
  // The pointer is now held by the Activity as its ANativeActivity::instance,
  // until the destruction.
  if (!app_context->InitializeApp(app_creator)) {
    delete app_context;
    ANativeActivity_finish(activity);
  }
}

AndroidWindowedAppContext::~AndroidWindowedAppContext() {
  // TODO(Triang3l): Unregister activity callbacks.
  activity_->instance = nullptr;
}

void AndroidWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  // TODO(Triang3l): Request message processing in the UI thread.
}

void AndroidWindowedAppContext::PlatformQuitFromUIThread() {
  ANativeActivity_finish(activity);
}

AndroidWindowedAppContext::AndroidWindowedAppContext(ANativeActivity* activity)
    : activity_(activity) {
  activity_->instance = this;
  // TODO(Triang3l): Register activity callbacks.
}

bool AndroidWindowedAppContext::InitializeApp(std::unique_ptr<WindowedApp> (
    *app_creator)(WindowedAppContext& app_context)) {
  assert_null(app_);
  app_ = app_creator(this);
  if (!app_->OnInitialize()) {
    app_->InvokeOnDestroy();
    app_.reset();
    return false;
  }
  return true;
}

}  // namespace ui
}  // namespace xe
