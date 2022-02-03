/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/file_picker.h"

#include <filesystem>
#include <string>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/platform_linux.h"
#include "xenia/base/string.h"
#include "xenia/ui/window_gtk.h"

namespace xe {
namespace ui {

class GtkFilePicker : public FilePicker {
 public:
  GtkFilePicker();
  ~GtkFilePicker() override;

  bool Show(Window* parent_window) override;

 private:
};

std::unique_ptr<FilePicker> FilePicker::Create() {
  return std::make_unique<GtkFilePicker>();
}

GtkFilePicker::GtkFilePicker() = default;

GtkFilePicker::~GtkFilePicker() = default;

bool GtkFilePicker::Show(Window* parent_window) {
  // TODO(benvanik): FileSaveDialog.
  assert_true(mode() == Mode::kOpen);
  // TODO(benvanik): folder dialogs.
  assert_true(type() == Type::kFile);
  GtkWidget* dialog;
  gint res;

  dialog = gtk_file_chooser_dialog_new(
      "Open File",
      parent_window
          ? GTK_WINDOW(static_cast<const GTKWindow*>(parent_window)->window())
          : nullptr,
      GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open",
      GTK_RESPONSE_ACCEPT, NULL);

  res = gtk_dialog_run(GTK_DIALOG(dialog));
  char* filename;
  if (res == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    filename = gtk_file_chooser_get_filename(chooser);
    std::vector<std::filesystem::path> selected_files;
    selected_files.push_back(xe::to_path(std::string(filename)));
    set_selected_files(selected_files);
    gtk_widget_destroy(dialog);
    return true;
  }
  gtk_widget_destroy(dialog);
  return false;
}

}  // namespace ui
}  // namespace xe
