/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace xe {
namespace filesystem {

bool PathExists(const std::wstring& path) {
  struct stat st;
  return stat(xe::to_string(path).c_str(), &st) == 0;
}

FILE* OpenFile(const std::wstring& path, const char* mode) {
  auto fixed_path = xe::fix_path_separators(path);
  return fopen(xe::to_string(fixed_path).c_str(), mode);
}

bool CreateFolder(const std::wstring& path) {
  return mkdir(xe::to_string(path).c_str(), 0774);
}

std::vector<FileInfo> ListFiles(const std::wstring& path) {
  std::vector<FileInfo> result;

  DIR* dir = opendir(xe::to_string(path).c_str());
  if (!dir) {
    return result;
  }

  while (auto ent = readdir(dir)) {
    FileInfo info;
    if (ent->d_type == DT_DIR) {
      info.type = FileInfo::Type::kDirectory;
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size = 0;  // TODO(DrChat): Find a way to get this
    }

    info.create_timestamp = 0;
    info.access_timestamp = 0;
    info.write_timestamp = 0;
    info.name = xe::to_wstring(ent->d_name);
    result.push_back(info);
  }

  return result;
}

}  // namespace filesystem
}  // namespace xe