/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem.h"

#include <shellapi.h>

#include <string>

#include "xenia/base/platform.h"

namespace xe {
namespace filesystem {

bool PathExists(const std::wstring& path) {
  DWORD attrib = GetFileAttributes(path.c_str());
  return attrib != INVALID_FILE_ATTRIBUTES;
}

bool CreateFolder(const std::wstring& path) {
  wchar_t folder[MAX_PATH] = {0};
  auto end = std::wcschr(path.c_str(), L'\\');
  while (end) {
    wcsncpy(folder, path.c_str(), end - path.c_str() + 1);
    CreateDirectory(folder, NULL);
    end = wcschr(++end, L'\\');
  }
  return PathExists(path);
}

bool DeleteFolder(const std::wstring& path) {
  auto double_null_path = path + L"\0";
  SHFILEOPSTRUCT op = {0};
  op.wFunc = FO_DELETE;
  op.pFrom = double_null_path.c_str();
  op.fFlags = FOF_NO_UI;
  return SHFileOperation(&op) == 0;
}

std::vector<FileInfo> ListFiles(const std::wstring& path) {
  std::vector<FileInfo> result;

  WIN32_FIND_DATA ffd;
  HANDLE handle = FindFirstFile((path + L"\\*").c_str(), &ffd);
  if (handle == INVALID_HANDLE_VALUE) {
    return result;
  }
  do {
    if (std::wcscmp(ffd.cFileName, L".") == 0 ||
        std::wcscmp(ffd.cFileName, L"..") == 0) {
      continue;
    }
    FileInfo info;
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      info.type = FileInfo::Type::kDirectory;
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size =
          (ffd.nFileSizeHigh * (size_t(MAXDWORD) + 1)) + ffd.nFileSizeLow;
    }
    info.name = ffd.cFileName;
    result.push_back(info);
  } while (FindNextFile(handle, &ffd) != 0);
  FindClose(handle);

  return result;
}

}  // namespace filesystem
}  // namespace xe
