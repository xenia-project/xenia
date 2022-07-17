/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>

#include "xenia/ui/file_picker.h"

namespace xe {
namespace ui {

// TODO(Triang3l): An asynchronous file picker with a callback, starting an
// activity for an ACTION_OPEN_DOCUMENT or an ACTION_OPEN_DOCUMENT_TREE intent.
// This intent, however, provides a content URI, not the file path directly.
// Accessing the file via the Storage Access Framework doesn't require the
// READ_EXTERNAL_STORAGE permission, unlike opening a file by its path directly.
// A file descriptor can be opened for the URI using
// Context.getContentResolver().openFileDescriptor (it will return a
// ParcelFileDescriptor, and a FileDescriptor can be obtained using its
// getFileDescriptor() method).

class AndroidFilePicker : public FilePicker {
 public:
  bool Show(Window* parent_window) override { return false; }
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<AndroidFilePicker>();
}

}  // namespace ui
}  // namespace xe
