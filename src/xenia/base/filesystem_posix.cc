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
#include <fcntl.h>
#include <ftw.h>
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

static int removeCallback(const char* fpath, const struct stat* sb,
                          int typeflag, struct FTW* ftwbuf) {
  int rv = remove(fpath);
  return rv;
}

bool DeleteFolder(const std::wstring& path) {
  return nftw(xe::to_string(path).c_str(), removeCallback, 64,
              FTW_DEPTH | FTW_PHYS) == 0
             ? true
             : false;
}

static uint64_t convertUnixtimeToWinFiletime(time_t unixtime) {
  // Linux uses number of seconds since 1/1/1970, and Windows uses
  // number of nanoseconds since 1/1/1601
  // so we convert linux time to nanoseconds and then add the number of
  // nanoseconds from 1601 to 1970
  // see https://msdn.microsoft.com/en-us/library/ms724228
  uint64_t filetime = filetime = (unixtime * 10000000) + 116444736000000000;
  return filetime;
}

bool IsFolder(const std::wstring& path) {
  struct stat st;
  if (stat(xe::to_string(path).c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode)) return true;
  }
  return false;
}

bool CreateFile(const std::wstring& path) {
  int file = creat(xe::to_string(path).c_str(), 0774);
  if (file >= 0) {
    close(file);
    return true;
  }
  return false;
}

bool DeleteFile(const std::wstring& path) {
  return (xe::to_string(path).c_str()) == 0 ? true : false;
}

class PosixFileHandle : public FileHandle {
 public:
  PosixFileHandle(std::wstring path, int handle)
      : FileHandle(std::move(path)), handle_(handle) {}
  ~PosixFileHandle() override {
    close(handle_);
    handle_ = -1;
  }
  bool Read(size_t file_offset, void* buffer, size_t buffer_length,
            size_t* out_bytes_read) override {
    ssize_t out = pread(handle_, buffer, buffer_length, file_offset);
    *out_bytes_read = out;
    return out >= 0 ? true : false;
  }
  bool Write(size_t file_offset, const void* buffer, size_t buffer_length,
             size_t* out_bytes_written) override {
    ssize_t out = pwrite(handle_, buffer, buffer_length, file_offset);
    *out_bytes_written = out;
    return out >= 0 ? true : false;
  }
  void Flush() override { fsync(handle_); }

 private:
  int handle_ = -1;
};

std::unique_ptr<FileHandle> FileHandle::OpenExisting(std::wstring path,
                                                     uint32_t desired_access) {
  int open_access;
  if (desired_access & FileAccess::kGenericRead) {
    open_access |= O_RDONLY;
  }
  if (desired_access & FileAccess::kGenericWrite) {
    open_access |= O_WRONLY;
  }
  if (desired_access & FileAccess::kGenericExecute) {
    open_access |= O_RDONLY;
  }
  if (desired_access & FileAccess::kGenericAll) {
    open_access |= O_RDWR;
  }
  if (desired_access & FileAccess::kFileReadData) {
    open_access |= O_RDONLY;
  }
  if (desired_access & FileAccess::kFileWriteData) {
    open_access |= O_WRONLY;
  }
  if (desired_access & FileAccess::kFileAppendData) {
    open_access |= O_APPEND;
  }
  int handle = open(xe::to_string(path).c_str(), open_access);
  if (handle == -1) {
    // TODO(benvanik): pick correct response.
    return nullptr;
  }
  return std::make_unique<PosixFileHandle>(path, handle);
}

bool GetInfo(const std::wstring& path, FileInfo* out_info) {
  struct stat st;
  if (stat(xe::to_string(path).c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      out_info->type = FileInfo::Type::kDirectory;
    } else {
      out_info->type = FileInfo::Type::kFile;
    }
    out_info->create_timestamp = convertUnixtimeToWinFiletime(st.st_ctime);
    out_info->access_timestamp = convertUnixtimeToWinFiletime(st.st_atime);
    out_info->write_timestamp = convertUnixtimeToWinFiletime(st.st_mtime);
    return true;
  }
  return false;
}

std::vector<FileInfo> ListFiles(const std::wstring& path) {
  std::vector<FileInfo> result;

  DIR* dir = opendir(xe::to_string(path).c_str());
  if (!dir) {
    return result;
  }

  while (auto ent = readdir(dir)) {
    FileInfo info;

    info.name = xe::to_wstring(ent->d_name);
    struct stat st;
    stat((xe::to_string(path) + xe::to_string(info.name)).c_str(), &st);
    info.create_timestamp = convertUnixtimeToWinFiletime(st.st_ctime);
    info.access_timestamp = convertUnixtimeToWinFiletime(st.st_atime);
    info.write_timestamp = convertUnixtimeToWinFiletime(st.st_mtime);
    if (ent->d_type == DT_DIR) {
      info.type = FileInfo::Type::kDirectory;
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size = st.st_size;
    }
    result.push_back(info);
  }

  return result;
}

}  // namespace filesystem
}  // namespace xe
