/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_UI_OPEN_POSTMORTEM_TRACE_DIALOG_H_
#define XDB_UI_OPEN_POSTMORTEM_TRACE_DIALOG_H_

#include <xdb/ui/xdb_ui.h>

namespace xdb {
namespace ui {

class OpenPostmortemTraceDialog : public OpenPostmortemTraceDialogBase {
 public:
  OpenPostmortemTraceDialog() : OpenPostmortemTraceDialogBase(nullptr) {}

  const std::wstring trace_file_path() const {
    return trace_file_picker_->GetFileName().GetFullPath().ToStdWstring();
  }
  const std::wstring content_file_path() const {
    return content_file_picker_->GetFileName().GetFullPath().ToStdWstring();
  }
};

}  // namespace ui
}  // namespace xdb

#endif  // XDB_UI_OPEN_POSTMORTEM_TRACE_DIALOG_H_
