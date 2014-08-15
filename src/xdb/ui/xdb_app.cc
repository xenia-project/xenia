/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/ui/xdb_app.h>

#include <atomic>
#include <codecvt>
#include <thread>

#include <poly/assert.h>
#include <xdb/postmortem_debug_target.h>
#include <xdb/ui/main_frame.h>
#include <xdb/ui/open_postmortem_trace_dialog.h>
#include <third_party/wxWidgets/include/wx/progdlg.h>

namespace xdb {
namespace ui {

IMPLEMENT_APP(XdbApp);

bool XdbApp::OnInit() { return true; }

int XdbApp::OnExit() {
  // Top level windows are deleted by wx automatically.
  return 0;
}

void XdbApp::OpenEmpty() {
  while (true) {
    OpenPostmortemTraceDialog dialog;
    if (dialog.ShowModal() == wxID_CANCEL) {
      Exit();
      return;
    }
    if (OpenTraceFile(dialog.trace_file_path(), dialog.content_file_path())) {
      break;
    }
  }
}

bool XdbApp::OpenTraceFile(const std::string& trace_file_path,
                           const std::string& content_file_path) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return OpenTraceFile(converter.from_bytes(trace_file_path),
                       converter.from_bytes(content_file_path));
}

bool XdbApp::OpenTraceFile(const std::wstring& trace_file_path,
                           const std::wstring& content_file_path) {
  std::unique_ptr<PostmortemDebugTarget> target(new PostmortemDebugTarget());

  if (!target->LoadTrace(trace_file_path)) {
    HandleOpenError("Unable to load trace file.");
    return false;
  }

  if (!target->LoadContent(content_file_path)) {
    HandleOpenError("Unable to load source game content module.");
    return false;
  }

  wxProgressDialog progress_dialog(
      "Preparing trace...", "This may take some time.", 100, nullptr,
      wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH | wxPD_CAN_ABORT |
          wxPD_ELAPSED_TIME | wxPD_ELAPSED_TIME);
  progress_dialog.Show();

  enum class PrepareStatus {
    PROCESSING,
    SUCCEEDED,
    FAILED,
  };
  std::atomic<PrepareStatus> status(PrepareStatus::PROCESSING);
  std::atomic<bool> cancelled;
  std::thread preparation_thread([&target, &cancelled, &status]() {
    status = target->Prepare(cancelled) ? PrepareStatus::SUCCEEDED
                                        : PrepareStatus::FAILED;
  });

  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (status == PrepareStatus::PROCESSING && progress_dialog.Pulse());
  cancelled = progress_dialog.WasCancelled();
  preparation_thread.join();
  progress_dialog.Hide();
  if (cancelled) {
    return false;
  }
  if (status == PrepareStatus::FAILED) {
    HandleOpenError("Invalid trace file; unable to process.");
    return false;
  }

  return OpenDebugTarget(std::move(target));
}

bool XdbApp::OpenDebugTarget(std::unique_ptr<DebugTarget> debug_target) {
  auto main_frame = new MainFrame(std::move(debug_target));

  main_frames_.push_back(main_frame);
  main_frame->Connect(wxEVT_DESTROY, wxEventHandler(XdbApp::OnMainFrameDestroy),
                      nullptr, this);

  main_frame->Show();
  main_frame->SetFocus();

  return true;
}

void XdbApp::HandleOpenError(const std::string& message) {
  wxMessageDialog dialog(nullptr, message, "Error",
                         wxOK | wxOK_DEFAULT | wxICON_ERROR);
  dialog.ShowModal();
}

void XdbApp::OnMainFrameDestroy(wxEvent& event) {
  for (auto it = main_frames_.begin(); it != main_frames_.end(); ++it) {
    if (*it == event.GetEventObject()) {
      main_frames_.erase(it);
      return;
    }
  }
  assert_always();
}

}  // namespace ui
}  // namespace xdb
