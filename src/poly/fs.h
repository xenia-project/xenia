/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_FS_H_
#define POLY_FS_H_

#include <string>

#include "poly/config.h"
#include "poly/string.h"

namespace poly {
namespace fs {

bool PathExists(const std::wstring& path);

bool CreateFolder(const std::wstring& path);

bool DeleteFolder(const std::wstring& path);

struct FileInfo {
  enum class Type {
    kFile,
    kDirectory,
  };
  Type type;
  std::wstring name;
  size_t total_size;
};
std::vector<FileInfo> ListFiles(const std::wstring& path);

}  // namespace fs
}  // namespace poly

#endif  // POLY_FS_H_
