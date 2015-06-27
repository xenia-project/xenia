/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WIN32_WIN32_FILE_PICKER_H_
#define XENIA_UI_WIN32_WIN32_FILE_PICKER_H_

#include "xenia/ui/file_picker.h"

namespace xe {
namespace ui {
namespace win32 {

class Win32FilePicker : public FilePicker {
 public:
  Win32FilePicker();
  ~Win32FilePicker() override;

  bool Show(void* parent_window_handle) override;

 private:
};

}  // namespace win32
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WIN32_WIN32_FILE_PICKER_H_
