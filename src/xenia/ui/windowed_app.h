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
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app_context.h"

#if XE_PLATFORM_ANDROID
// Multiple apps in a single library instead of separate executables.
#define XE_UI_WINDOWED_APPS_IN_LIBRARY 1
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

  using Creator = std::unique_ptr<xe::ui::WindowedApp> (*)(
      xe::ui::WindowedAppContext& app_context);

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

#if XE_UI_WINDOWED_APPS_IN_LIBRARY
 public:
  class CreatorRegistration {
   public:
    CreatorRegistration(const std::string_view identifier, Creator creator) {
      if (!creators_) {
        // Will be deleted by the last creator registration's destructor, no
        // need for a library destructor.
        creators_ = new std::unordered_map<std::string, WindowedApp::Creator>;
      }
      iterator_inserted_ = creators_->emplace(identifier, creator);
      assert_true(iterator_inserted_.second);
    }

    ~CreatorRegistration() {
      if (iterator_inserted_.second) {
        creators_->erase(iterator_inserted_.first);
        if (creators_->empty()) {
          delete creators_;
        }
      }
    }

   private:
    std::pair<std::unordered_map<std::string, Creator>::iterator, bool>
        iterator_inserted_;
  };

  static Creator GetCreator(const std::string& identifier) {
    if (!creators_) {
      return nullptr;
    }
    auto it = creators_->find(identifier);
    return it != creators_->end() ? it->second : nullptr;
  }

 private:
  static std::unordered_map<std::string, Creator>* creators_;
#endif  // XE_UI_WINDOWED_APPS_IN_LIBRARY
};

#if XE_UI_WINDOWED_APPS_IN_LIBRARY
// Multiple apps in a single library.
#define XE_DEFINE_WINDOWED_APP(identifier, creator)                          \
  namespace xe {                                                             \
  namespace ui {                                                             \
  namespace windowed_app_creator_registrations {                             \
  xe::ui::WindowedApp::CreatorRegistration identifier(#identifier, creator); \
  }                                                                          \
  }                                                                          \
  }
#else
// Separate executables for each app.
std::unique_ptr<WindowedApp> (*GetWindowedAppCreator())(
    WindowedAppContext& app_context);
#define XE_DEFINE_WINDOWED_APP(identifier, creator)              \
  xe::ui::WindowedApp::Creator xe::ui::GetWindowedAppCreator() { \
    return creator;                                              \
  }
#endif  // XE_UI_WINDOWED_APPS_IN_LIBRARY

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_H_
