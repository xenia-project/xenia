/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#include <codecvt>
#include <locale>
#include <string>
#include "xenia/base/assert.h"
#include "xenia/base/platform_linux.h"

namespace xe {
namespace ui {

class GtkFilePicker : public FilePicker {
 public:
  GtkFilePicker();
  ~GtkFilePicker() override;

  bool Show(void* parent_window_handle) override;

 private:
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<GtkFilePicker>();
}

GtkFilePicker::GtkFilePicker() = default;

GtkFilePicker::~GtkFilePicker() = default;

bool GtkFilePicker::Show(void* parent_window_handle) {
  // TODO(benvanik): FileSaveDialog.
  assert_true(mode() == Mode::kOpen);
  // TODO(benvanik): folder dialogs.
  assert_true(type() == Type::kFile);
  GtkWidget* dialog;
  gint res;

  dialog = gtk_file_chooser_dialog_new(
      "Open File", (GtkWindow*)parent_window_handle,
      GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
      GTK_RESPONSE_ACCEPT, NULL);

  res = gtk_dialog_run(GTK_DIALOG(dialog));
  char* filename;
  if (res == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    filename = gtk_file_chooser_get_filename(chooser);
    std::vector<std::wstring> selected_files;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring ws_filename = converter.from_bytes(filename);
    selected_files.push_back(ws_filename);
    set_selected_files(selected_files);
    gtk_widget_destroy(dialog);
    return true;
  }
  gtk_widget_destroy(dialog);
  return false;
}

}  // namespace ui
}  // namespace xe
