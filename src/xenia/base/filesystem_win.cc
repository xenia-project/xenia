/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

#include <string>

#include "xenia/base/platform_win.h"

namespace xe {
namespace filesystem {

bool PathExists(const std::wstring& path) {
  DWORD attrib = GetFileAttributes(path.c_str());
  return attrib != INVALID_FILE_ATTRIBUTES;
}

bool CreateFolder(const std::wstring& path) {
  wchar_t folder[kMaxPath] = {0};
  auto end = std::wcschr(path.c_str(), xe::kWPathSeparator);
  while (end) {
    wcsncpy(folder, path.c_str(), end - path.c_str() + 1);
    CreateDirectory(folder, NULL);
    end = wcschr(++end, xe::kWPathSeparator);
  }
  return PathExists(path);
}

bool DeleteFolder(const std::wstring& path) {
  auto double_null_path = path + std::wstring(L"\0", 1);
  SHFILEOPSTRUCT op = {0};
  op.wFunc = FO_DELETE;
  op.pFrom = double_null_path.c_str();
  op.fFlags = FOF_NO_UI;
  return SHFileOperation(&op) == 0;
}

bool IsFolder(const std::wstring& path) {
  DWORD attrib = GetFileAttributes(path.c_str());
  return attrib != INVALID_FILE_ATTRIBUTES &&
         (attrib & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
}

#undef CreateFile
bool CreateFile(const std::wstring& path) {
  auto handle = CreateFileW(path.c_str(), 0, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(handle);
  return true;
}

FILE* OpenFile(const std::wstring& path, const char* mode) {
  auto fixed_path = xe::fix_path_separators(path);
  return _wfopen(fixed_path.c_str(), xe::to_wstring(mode).c_str());
}

bool DeleteFile(const std::wstring& path) {
  return DeleteFileW(path.c_str()) ? true : false;
}

class Win32FileHandle : public FileHandle {
 public:
  Win32FileHandle(std::wstring path, HANDLE handle)
      : FileHandle(std::move(path)), handle_(handle) {}
  ~Win32FileHandle() override {
    CloseHandle(handle_);
    handle_ = nullptr;
  }
  bool Read(size_t file_offset, void* buffer, size_t buffer_length,
            size_t* out_bytes_read) override {
    *out_bytes_read = 0;
    OVERLAPPED overlapped;
    overlapped.Pointer = (PVOID)file_offset;
    overlapped.hEvent = NULL;
    DWORD bytes_read = 0;
    BOOL read = ReadFile(handle_, buffer, (DWORD)buffer_length, &bytes_read,
                         &overlapped);
    if (read) {
      *out_bytes_read = bytes_read;
      return true;
    } else {
      if (GetLastError() == ERROR_NOACCESS) {
        XELOGW(
            "Win32FileHandle::Read(..., %.8llX, %.8llX, ...) returned "
            "ERROR_NOACCESS. Read-only memory?",
            buffer, buffer_length);
      }
      return false;
    }
  }
  bool Write(size_t file_offset, const void* buffer, size_t buffer_length,
             size_t* out_bytes_written) override {
    *out_bytes_written = 0;
    OVERLAPPED overlapped;
    overlapped.Pointer = (PVOID)file_offset;
    overlapped.hEvent = NULL;
    DWORD bytes_written = 0;
    BOOL wrote = WriteFile(handle_, buffer, (DWORD)buffer_length,
                           &bytes_written, &overlapped);
    if (wrote) {
      *out_bytes_written = bytes_written;
      return true;
    } else {
      return false;
    }
  }
  bool SetLength(size_t length) {
    LARGE_INTEGER position;
    position.QuadPart = length;
    if (!SetFilePointerEx(handle_, position, nullptr, SEEK_SET)) {
      return false;
    }
    if (!SetEndOfFile(handle_)) {
      return false;
    }
    return true;
  }
  void Flush() override { FlushFileBuffers(handle_); }

 private:
  HANDLE handle_ = nullptr;
};

std::unique_ptr<FileHandle> FileHandle::OpenExisting(std::wstring path,
                                                     uint32_t desired_access) {
  DWORD open_access = 0;
  if (desired_access & FileAccess::kGenericRead) {
    open_access |= GENERIC_READ;
  }
  if (desired_access & FileAccess::kGenericWrite) {
    open_access |= GENERIC_WRITE;
  }
  if (desired_access & FileAccess::kGenericExecute) {
    open_access |= GENERIC_EXECUTE;
  }
  if (desired_access & FileAccess::kGenericAll) {
    open_access |= GENERIC_READ | GENERIC_WRITE;
  }
  if (desired_access & FileAccess::kFileReadData) {
    open_access |= FILE_READ_DATA;
  }
  if (desired_access & FileAccess::kFileWriteData) {
    open_access |= FILE_WRITE_DATA;
  }
  if (desired_access & FileAccess::kFileAppendData) {
    open_access |= FILE_APPEND_DATA;
  }
  DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
  // We assume we've already created the file in the caller.
  DWORD creation_disposition = OPEN_EXISTING;
  HANDLE handle = CreateFileW(
      path.c_str(), open_access, share_mode, nullptr, creation_disposition,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    // TODO(benvanik): pick correct response.
    return nullptr;
  }
  return std::make_unique<Win32FileHandle>(path, handle);
}

#define COMBINE_TIME(t) (((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime)

bool GetInfo(const std::wstring& path, FileInfo* out_info) {
  std::memset(out_info, 0, sizeof(FileInfo));
  WIN32_FILE_ATTRIBUTE_DATA data = {0};
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data)) {
    return false;
  }
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    out_info->type = FileInfo::Type::kDirectory;
    out_info->total_size = 0;
  } else {
    out_info->type = FileInfo::Type::kFile;
    out_info->total_size =
        (data.nFileSizeHigh * (size_t(MAXDWORD) + 1)) + data.nFileSizeLow;
  }
  out_info->path = xe::find_base_path(path);
  out_info->name = xe::find_name_from_path(path);
  out_info->create_timestamp = COMBINE_TIME(data.ftCreationTime);
  out_info->access_timestamp = COMBINE_TIME(data.ftLastAccessTime);
  out_info->write_timestamp = COMBINE_TIME(data.ftLastWriteTime);
  return true;
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
    info.path = path;
    info.name = ffd.cFileName;
    info.create_timestamp = COMBINE_TIME(ffd.ftCreationTime);
    info.access_timestamp = COMBINE_TIME(ffd.ftLastAccessTime);
    info.write_timestamp = COMBINE_TIME(ffd.ftLastWriteTime);
    result.push_back(info);
  } while (FindNextFile(handle, &ffd) != 0);
  FindClose(handle);

  return result;
}

}  // namespace filesystem
}  // namespace xe
