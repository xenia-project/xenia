/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_XDB_APP_H_
#define XDB_XDB_APP_H_

#include <memory>

#include <xdb/ui/main_frame.h>
#include <third_party/wxWidgets/include/wx/wx.h>

namespace xdb {

class XdbApp : public wxApp {
 public:
  bool OnInit() override;
  int OnExit() override;

 private:
  std::unique_ptr<ui::MainFrame> main_frame_;
};

}  // namespace xdb

#endif  // XDB_XDB_APP_H_
