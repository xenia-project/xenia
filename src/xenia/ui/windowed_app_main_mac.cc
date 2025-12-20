/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context_mac.h"

extern "C" int main(int argc, char** argv) {
  int result;

  {
    // Debug: dump all incoming argv to see Xcode-injected flags.
    std::fprintf(stderr, "[xenia mac argv] argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
      std::fprintf(stderr, "  argv[%d]: %s\n", i, argv[i] ? argv[i] : "(null)");
    }
    std::fflush(stderr);

    // Strip macOS/Xcode-injected flags that aren't valid cvars.
    std::vector<std::string> filtered_args;
    filtered_args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
      std::string_view arg(argv[i]);
      if (arg.rfind("-NSDocumentRevisionsDebugMode", 0) == 0 ||
          arg.rfind("-ApplePersistenceIgnoreState", 0) == 0  ||
          arg.rfind("-NSDocumentRevisionsDebugMode", 0) == 0 ||
          arg.rfind("-YES", 0) == 0 ||
          arg.rfind("YES", 0) == 0) {
        continue;
      }
      filtered_args.emplace_back(argv[i]);
    }
    std::vector<char*> filtered_argv_vec;
    filtered_argv_vec.reserve(filtered_args.size());
    for (auto& s : filtered_args) {
      filtered_argv_vec.push_back(const_cast<char*>(s.c_str()));
    }
    int filtered_argc = static_cast<int>(filtered_argv_vec.size());
    char** filtered_argv = filtered_argv_vec.data();

    xe::ui::MacWindowedAppContext app_context;

    std::unique_ptr<xe::ui::WindowedApp> app =
        xe::ui::GetWindowedAppCreator()(app_context);

    cvar::ParseLaunchArguments(filtered_argc, filtered_argv,
                               app->GetPositionalOptionsUsage(),
                               app->GetPositionalOptions());

    // Initialize logging. Needs parsed cvars.
    xe::InitializeLogging(app->GetName());

    if (app->OnInitialize()) {
      app_context.RunMainCocoaLoop();
      result = EXIT_SUCCESS;
    } else {
      XELOGE("Failed to initialize app");
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  xe::ShutdownLogging();

  return result;
}
