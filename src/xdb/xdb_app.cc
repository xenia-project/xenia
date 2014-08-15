/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/xdb_app.h>

namespace xdb {

IMPLEMENT_APP(XdbApp);

bool XdbApp::OnInit() {
  main_frame_.reset(new ui::MainFrame());

  main_frame_->Show();
  return true;
}

int XdbApp::OnExit() {
  // Top level windows are deleted by wx automatically.
  main_frame_.release();
  return 0;
}

}  // namespace xdb
