/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem.h"

#include <algorithm>

namespace xe {
namespace filesystem {

bool CreateParentFolder(const std::filesystem::path& path) {
  if (path.has_parent_path()) {
    auto parent_path = path.parent_path();
    if (!std::filesystem::exists(parent_path)) {
      return std::filesystem::create_directories(parent_path);
    }
  }
  return true;
}

std::vector<FileInfo> ListDirectories(const std::filesystem::path& path) {
  std::vector<FileInfo> files = ListFiles(path);
  std::vector<FileInfo> directories = {};

  std::copy_if(files.cbegin(), files.cend(), std::back_inserter(directories),
               [](const FileInfo& file) {
                 return file.type == FileInfo::Type::kDirectory;
               });

  return std::move(directories);
}

std::vector<FileInfo> FilterByName(const std::vector<FileInfo>& files,
                                   const std::regex pattern) {
  std::vector<FileInfo> filtered_entries = {};

  std::copy_if(
      files.cbegin(), files.cend(), std::back_inserter(filtered_entries),
      [pattern](const FileInfo& file) {
        return std::regex_match(file.name.filename().string(), pattern);
      });
  return std::move(filtered_entries);
}

}  // namespace filesystem
}  // namespace xe
