/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_FILE_PICKER_H_
#define XENIA_UI_FILE_PICKER_H_

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/ui/window.h"

namespace xe {
namespace ui {

class FilePicker {
 public:
  enum class Mode {
    kOpen = 0,
    kSave = 1,
  };
  enum class Type {
    kFile = 0,
    kDirectory = 1,
  };

  static std::unique_ptr<FilePicker> Create();

  FilePicker()
      : mode_(Mode::kOpen),
        type_(Type::kFile),
        title_("Select Files"),
        multi_selection_(false) {}
  virtual ~FilePicker() = default;

  Mode mode() const { return mode_; }
  void set_mode(Mode mode) { mode_ = mode; }

  Type type() const { return type_; }
  void set_type(Type type) { type_ = type; }

  const std::string& title() const { return title_; }
  void set_title(std::string title) { title_ = std::move(title); }

  const std::string& default_extension() const { return default_extension_; }
  void set_default_extension(std::string default_extension) {
    default_extension_ = std::move(default_extension);
  }

  const std::string& file_name() const { return file_name_; }
  void set_file_name(std::string file_name) {
    file_name_ = std::move(file_name);
  }

  std::vector<std::pair<std::string, std::string>> extensions() const {
    return extensions_;
  }
  void set_extensions(
      std::vector<std::pair<std::string, std::string>> extensions) {
    extensions_ = std::move(extensions);
  }

  bool multi_selection() const { return multi_selection_; }
  void set_multi_selection(bool multi_selection) {
    multi_selection_ = multi_selection;
  }

  std::vector<std::filesystem::path> selected_files() const {
    return selected_files_;
  }
  void set_selected_files(std::vector<std::filesystem::path> selected_files) {
    selected_files_ = std::move(selected_files);
  }

  virtual bool Show(Window* parent_window = nullptr) = 0;

 private:
  Mode mode_;
  Type type_;
  std::string title_;
  std::string default_extension_;
  std::string file_name_;
  std::vector<std::pair<std::string, std::string>> extensions_;
  bool multi_selection_;

  std::vector<std::filesystem::path> selected_files_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_FILE_PICKER_H_
