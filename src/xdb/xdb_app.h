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
#include <string>
#include <vector>

#include <xdb/debug_target.h>
#include <third_party/wxWidgets/include/wx/wx.h>

namespace xdb {
namespace ui {
class MainFrame;
}  // namespace ui
}  // namespace xdb

namespace xdb {

class XdbApp : public wxApp {
 public:
  bool OnInit() override;
  int OnExit() override;

  void OpenEmpty();
  bool OpenTraceFile(const std::string& trace_file_path,
                     const std::string& content_file_path = "");
  bool OpenTraceFile(const std::wstring& trace_file_path,
                     const std::wstring& content_file_path = L"");
  bool OpenDebugTarget(std::unique_ptr<DebugTarget> debug_target);

 private:
  void HandleOpenError(const std::string& message);

  void OnMainFrameDestroy(wxEvent& event);

  std::vector<ui::MainFrame*> main_frames_;
};

}  // namespace xdb

#endif  // XDB_XDB_APP_H_
