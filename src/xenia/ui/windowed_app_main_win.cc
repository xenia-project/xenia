/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>
#include <memory>

#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/main_win.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_win.h"

DEFINE_bool(enable_console, false, "Open a console window with the main window",
            "General");

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev,
                    LPWSTR command_line, int show_cmd) {
  int result;

  {
    xe::ui::Win32WindowedAppContext app_context(hinstance, show_cmd);
    // TODO(Triang3l): Initialize creates a window. Set DPI awareness via the
    // manifest.
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    if (!xe::ParseWin32LaunchArguments(false, app->GetPositionalOptionsUsage(),
                                       app->GetPositionalOptions(), nullptr)) {
      return EXIT_FAILURE;
    }

    // TODO(Triang3l): Rework this, need to initialize the console properly,
    // disable has_console_attached_ by default in windowed apps, and attach
    // only if needed.
    if (cvars::enable_console) {
      xe::AttachConsole();
    }

    // Initialize COM on the UI thread with the apartment-threaded concurrency
    // model, so dialogs can be used.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }

    xe::InitializeWin32App(app->GetName());

    result =
        app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownWin32App();

  CoUninitialize();

  return result;
}
