/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_H_
#define XENIA_UI_WINDOWED_APP_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app_context.h"

#if XE_PLATFORM_ANDROID
#include <android/native_activity.h>

#include "xenia/ui/windowed_app_context_android.h"
#endif

namespace xe {
namespace ui {

// Interface between the platform's entry points (in the main, UI, thread that
// also runs the message loop) and the app that implements it.
class WindowedApp {
 public:
  // WindowedApps are expected to provide a static creation function, for
  // creating an instance of the class (which may be called before
  // initialization of platform-specific parts, should preferably be as simple
  // as possible).

  WindowedApp(const WindowedApp& app) = delete;
  WindowedApp& operator=(const WindowedApp& app) = delete;
  virtual ~WindowedApp() = default;

  WindowedAppContext& app_context() const { return app_context_; }

  // Same as the executable (project), xenia-library-app.
  const std::string& GetName() const { return name_; }
  const std::string& GetPositionalOptionsUsage() const {
    return positional_options_usage_;
  }
  const std::vector<std::string>& GetPositionalOptions() const {
    return positional_options_;
  }

  // Called once before receiving other lifecycle callback invocations. Cvars
  // will be initialized with the launch arguments. Returns whether the app has
  // been initialized successfully (otherwise platform-specific code must call
  // OnDestroy and refuse to continue running the app).
  virtual bool OnInitialize() = 0;
  // See OnDestroy for more info.
  void InvokeOnDestroy() {
    // For safety and convenience of referencing objects owned by the app in
    // pending functions queued in or after OnInitialize, make sure they are
    // executed before telling the app that destruction needs to happen.
    app_context().ExecutePendingFunctionsFromUIThread();
    OnDestroy();
  }

 protected:
  // Positional options should be initialized in the constructor if needed.
  // Cvars will not have been initialized with the arguments at the moment of
  // construction (as the result depends on construction).
  explicit WindowedApp(
      WindowedAppContext& app_context, const std::string_view name,
      const std::string_view positional_options_usage = std::string_view())
      : app_context_(app_context),
        name_(name),
        positional_options_usage_(positional_options_usage) {}

  // For calling from the constructor.
  void AddPositionalOption(const std::string_view option) {
    positional_options_.emplace_back(option);
  }

  // OnDestroy entry point may be called (through InvokeOnDestroy) by the
  // platform-specific lifecycle interface at request of either the app itself
  // or the OS - thus should be possible for the lifecycle interface to call at
  // any moment (not from inside other lifecycle callbacks though). The app will
  // also be destroyed when that happens, so the destructor will also be called
  // (but this is more safe with respect to exceptions). This is only guaranteed
  // to be called if OnInitialize has already happened (successfully or not) -
  // in case of an error before initialization, the destructor may be called
  // alone as well. Context's pending functions will be executed before the
  // call, so it's safe to destroy dependencies of them here (though it may
  // still be possible to add more pending functions here depending on whether
  // the context was explicitly shut down before this is invoked).
  virtual void OnDestroy() {}

 private:
  WindowedAppContext& app_context_;

  std::string name_;
  std::string positional_options_usage_;
  std::vector<std::string> positional_options_;
};

#if XE_PLATFORM_ANDROID
// Multiple apps in a single library. ANativeActivity_onCreate chosen via
// android.app.func_name of the NativeActivity of each app.
#define XE_DEFINE_WINDOWED_APP(export_name, creator)                           \
  __attribute__((visibility("default"))) extern "C" void export_name(          \
      ANativeActivity* activity, void* saved_state, size_t saved_state_size) { \
    xe::ui::AndroidWindowedAppContext::StartAppOnActivityCreate(               \
        activity, saved_state, saved_state_size, creator);                     \
  }
#else
// Separate executables for each app.
std::unique_ptr<WindowedApp> (*GetWindowedAppCreator())(
    WindowedAppContext& app_context);
#define XE_DEFINE_WINDOWED_APP(export_name, creator)                       \
  std::unique_ptr<xe::ui::WindowedApp> (*xe::ui::GetWindowedAppCreator())( \
      xe::ui::WindowedAppContext & app_context) {                          \
    return creator;                                                        \
  }
#endif

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_H_
