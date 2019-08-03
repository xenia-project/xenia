/**
 ******************************************************************************
 * api-scanner - Scan for API imports from a packaged 360 game                *
 ******************************************************************************
 * Copyright 2015 x1nixmzeng. All rights reserved.                            *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "api_scanner_loader.h"

namespace xe {
namespace tools {

DEFINE_string(target, "", "List of file to extract imports from");

int api_scanner_main(std::vector<std::wstring>& args) {
  // XXX we need gflags to split multiple flags into arrays for us

  if (args.size() == 2 || !FLAGS_target.empty()) {
    apiscanner_loader loader_;
    std::wstring target(cvars::target.empty() ? args[1]
                                              : xe::to_wstring(cvars::target));

    std::wstring target_abs = xe::to_absolute_path(target);

    // XXX For each target?
    if (loader_.LoadTitleImports(target)) {
      for (const auto title : loader_.GetAllTitles()) {
        printf("%08x\n", title.title_id);
        for (const auto import : title.imports) {
          printf("\t%s\n", import.c_str());
        }
      }
    }
  }

  return 0;
}

}  // namespace tools
}  // namespace xe

DEFINE_ENTRY_POINT(L"api-scanner", L"api-scanner --target=<target file>",
                   xe::tools::api_scanner_main);
