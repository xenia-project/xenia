/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/xdb.h>

#include <memory>

#include <gflags/gflags.h>
#include <poly/main.h>
#include <poly/poly.h>
#include <third_party/wxWidgets/include/wx/wx.h>
#include <xdb/ui/xdb_app.h>

DEFINE_string(trace_file, "", "Trace file to load on startup.");
DEFINE_string(content_file, "",
              "ISO/STFS/XEX file the specified trace_file should reference.");

namespace xdb {

int main(std::vector<std::wstring>& args) {
  wxInitializer init;
  if (!init.IsOk()) {
    PFATAL("Failed to initialize wxWidgets");
    return 1;
  }

  // App is auto-freed by wx.
  auto app = new ui::XdbApp();
  wxApp::SetInstance(app);
  if (!wxEntryStart(0, nullptr)) {
    PFATAL("Failed to enter wxWidgets app");
    return 1;
  }
  if (!app->OnInit()) {
    PFATAL("Failed to init app");
    return 1;
  }

  if (!FLAGS_trace_file.empty()) {
    // Trace file specified on command line.
    if (!app->OpenTraceFile(FLAGS_trace_file, FLAGS_content_file)) {
      PFATAL("Failed to open trace file");
      return 1;
    }
  } else {
    app->OpenEmpty();
  }

  app->OnRun();
  int result_code = app->OnExit();
  wxEntryCleanup();
  return result_code;
}

}  // namespace xdb

DEFINE_ENTRY_POINT(L"xenia-debug", L"xenia-debug", xdb::main);
