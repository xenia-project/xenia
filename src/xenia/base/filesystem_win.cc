/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <io.h>
#include <shlobj.h>

#include <string>

#undef CreateFile

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"

namespace xe {

std::string path_to_utf8(const std::filesystem::path& path) {
  return xe::to_utf8(path.u16string());
}

std::u16string path_to_utf16(const std::filesystem::path& path) {
  return path.u16string();
}

std::filesystem::path to_path(const std::string_view source) {
  return xe::to_utf16(source);
}

std::filesystem::path to_path(const std::u16string_view source) {
  return source;
}

namespace filesystem {

std::filesystem::path GetExecutablePath() {
  wchar_t* path;
  auto error = _get_wpgmptr(&path);
  return !error ? std::filesystem::path(path) : std::filesystem::path();
}

std::filesystem::path GetExecutableFolder() {
  return GetExecutablePath().parent_path();
}

std::filesystem::path GetUserFolder() {
  std::filesystem::path result;
  PWSTR path;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT,
                                     nullptr, &path))) {
    result.assign(path);
    CoTaskMemFree(path);
  }
  return result;
}

bool CreateEmptyFile(const std::filesystem::path& path) {
  auto handle = CreateFileW(path.c_str(), 0, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(handle);
  return true;
}

FILE* OpenFile(const std::filesystem::path& path, const std::string_view mode) {
  // Dumb, but OK.
  const auto wmode = xe::to_utf16(mode);
  return _wfopen(path.c_str(), reinterpret_cast<const wchar_t*>(wmode.c_str()));
}

bool Seek(FILE* file, int64_t offset, int origin) {
  return _fseeki64(file, offset, origin) == 0;
}

int64_t Tell(FILE* file) { return _ftelli64(file); }

bool TruncateStdioFile(FILE* file, uint64_t length) {
  // Flush is necessary - if not flushing, stream position may be out of sync.
  if (fflush(file)) {
    return false;
  }
  int64_t position = Tell(file);
  if (position < 0) {
    return false;
  }
  if (_chsize_s(_fileno(file), int64_t(length))) {
    return false;
  }
  if (uint64_t(position) > length) {
    if (!Seek(file, 0, SEEK_END)) {
      return false;
    }
  }
  return true;
}

class Win32FileHandle : public FileHandle {
 public:
  Win32FileHandle(const std::filesystem::path& path, HANDLE handle)
      : FileHandle(path), handle_(handle) {}
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
            "Win32FileHandle::Read(..., {}, 0x{:X}, ...) returned "
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
  bool SetLength(size_t length) override {
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

std::unique_ptr<FileHandle> FileHandle::OpenExisting(
    const std::filesystem::path& path, uint32_t desired_access) {
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

std::optional<FileInfo> GetInfo(const std::filesystem::path& path) {
  WIN32_FILE_ATTRIBUTE_DATA data = {0};
  if (!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data)) {
    return {};
  }

  FileInfo out_info{};

  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    out_info.type = FileInfo::Type::kDirectory;
    out_info.total_size = 0;
  } else {
    out_info.type = FileInfo::Type::kFile;
    out_info.total_size =
        (data.nFileSizeHigh * (size_t(MAXDWORD) + 1)) + data.nFileSizeLow;
  }
  out_info.path = path.parent_path();
  out_info.name = path.filename();
  out_info.create_timestamp = COMBINE_TIME(data.ftCreationTime);
  out_info.access_timestamp = COMBINE_TIME(data.ftLastAccessTime);
  out_info.write_timestamp = COMBINE_TIME(data.ftLastWriteTime);
  return std::move(out_info);
}

std::vector<FileInfo> ListFiles(const std::filesystem::path& path) {
  std::vector<FileInfo> result;

  WIN32_FIND_DATA ffd;
  HANDLE handle = FindFirstFileW((path / "*").c_str(), &ffd);
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

bool SetAttributes(const std::filesystem::path& path, uint64_t attributes) {
  return SetFileAttributes(path.c_str(), static_cast<DWORD>(attributes));
}

}  // namespace filesystem
}  // namespace xe
