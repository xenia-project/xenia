/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app.h"

#if XE_PLATFORM_MAC
#include "xenia/ui/windowed_app_context_mac.h"
#else
#include <gtk/gtk.h>
#include "xenia/ui/windowed_app_context_gtk.h"
#endif

extern "C" int main(int argc_pre_gtk, char** argv_pre_gtk) {
#if XE_PLATFORM_MAC
  // macOS native implementation
  int result;

  {
    xe::ui::MacWindowedAppContext app_context;

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(argc_pre_gtk, argv_pre_gtk,
                               app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

    // Initialize logging. Needs parsed cvars.
    xe::InitializeLogging(app->GetName());

    result = app->OnInitialize();
    if (result) {
      XELOGE("Failed to initialize app");
      app->InvokeOnDestroy();
      xe::ShutdownLogging();
      return result;
    }

    app_context.RunMainCocoaLoop();

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownLogging();

  return result;
#else
  // Linux GTK+ implementation
  // Before touching anything GTK+, make sure that when running on Wayland,
  // we'll still get an X11 (Xwayland) window
  setenv("GDK_BACKEND", "x11", 1);

  // Initialize GTK+, which will handle and remove its own arguments from argv.
  // Both GTK+ and Xenia use --option=value argument format (see man
  // gtk-options), however, it's meaningless to try to parse the same argument
  // both as a GTK+ one and as a cvar. Make GTK+ options take precedence in case
  // of a name collision, as there's an alternative way of setting Xenia options
  // (the config).
  int argc_post_gtk = argc_pre_gtk;
  char** argv_post_gtk = argv_pre_gtk;
  
  // GTK3 API - pass argc and argv parameters
  if (!gtk_init_check(&argc_post_gtk, &argv_post_gtk)) {
    // Logging has not been initialized yet.
    std::fputs("Failed to initialize GTK+\n", stderr);
    return EXIT_FAILURE;
  }

  int result;

  {
    xe::ui::GTKWindowedAppContext app_context;

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(argc_post_gtk, argv_post_gtk,
                               app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

    // Initialize logging. Needs parsed cvars.
    xe::InitializeLogging(app->GetName());

    if (app->OnInitialize()) {
      app_context.RunMainGTKLoop();
      result = EXIT_SUCCESS;
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownLogging();

  return result;
#endif
}
