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
#include <poly/poly.h>
#include <third_party/wxWidgets/include/wx/wx.h>
#include <xdb/xdb_app.h>

namespace xdb {

int main(int argc, xechar_t** argv) {
  // Create platform abstraction layer.
  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  if (xe_pal_init(pal_options)) {
    XEFATAL("Failed to initialize PAL");
    return 1;
  }

  wxInitializer init;
  if (!init.IsOk()) {
    XEFATAL("Failed to initialize wxWidgets");
    return 1;
  }

  // App is auto-freed by wx.
  auto app = new XdbApp();
  wxApp::SetInstance(app);
  if (!wxEntryStart(0, nullptr)) {
    XEFATAL("Failed to enter wxWidgets app");
    return 1;
  }
  if (!app->OnInit()) {
    XEFATAL("Failed to init app");
    return 1;
  }
  app->OnRun();
  int result_code = app->OnExit();
  wxEntryCleanup();
  return result_code;
}

}  // namespace xdb

// TODO(benvanik): move main thunk into poly
// ehhh
XE_MAIN_WINDOW_THUNK(xdb::main, L"xenia-debug", "xenia-debug");
