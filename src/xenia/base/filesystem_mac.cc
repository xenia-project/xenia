// filesystem_posix.cc
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _POSIX_C_SOURCE 200112L  // Enable POSIX 2001 features

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

#include <sys/types.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace xe {

std::string path_to_utf8(const std::filesystem::path& path) {
  return path.string();
}

std::u16string path_to_utf16(const std::filesystem::path& path) {
  return xe::to_utf16(path.string());
}

std::filesystem::path to_path(const std::string_view source) { return source; }

std::filesystem::path to_path(const std::u16string_view source) {
  return xe::to_utf8(source);
}

namespace filesystem {

std::filesystem::path GetExecutablePath() {
#if defined(__APPLE__)
  char path[PATH_MAX];
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    char real_path[PATH_MAX];
    realpath(path, real_path);
    return std::string(real_path);
  } else {
    // Buffer too small; allocate the required size
    char* path2 = (char*)malloc(size);
    if (_NSGetExecutablePath(path2, &size) == 0) {
      char real_path[PATH_MAX];
      realpath(path2, real_path);
      std::string result(real_path);
      free(path2);
      return result;
    } else {
      // Shouldn't happen
      free(path2);
      return std::string();
    }
  }
#elif defined(__linux__)
  char buff[FILENAME_MAX] = "";
  ssize_t len = readlink("/proc/self/exe", buff, sizeof(buff) - 1);
  if (len != -1) {
    buff[len] = '\0';
    return std::string(buff);
  } else {
    // Error handling
    return std::string();
  }
#else
  // Other platforms
  return std::string();
#endif
}

std::filesystem::path GetExecutableFolder() {
  return GetExecutablePath().parent_path();
}

std::filesystem::path GetUserFolder() {
  // Get preferred data home
  char* home = std::getenv("XDG_DATA_HOME");
  if (home) {
    return std::string(home);
  }

  // If XDG_DATA_HOME not set, fallback to HOME directory
  home = std::getenv("HOME");

  // If HOME not set, fall back to this
  if (home == NULL) {
    struct passwd pw1;
    struct passwd* pw;
    char buf[4096];  // Could potentially lower this
    getpwuid_r(getuid(), &pw1, buf, sizeof(buf), &pw);
    assert(&pw1 == pw);  // Sanity check
    home = pw->pw_dir;
  }

  return std::filesystem::path(home) / ".local" / "share";
}

FILE* OpenFile(const std::filesystem::path& path, const std::string_view mode) {
  return fopen(path.c_str(), std::string(mode).c_str());
}

bool Seek(FILE* file, int64_t offset, int origin) {
  return fseeko(file, offset, origin) == 0;
}

int64_t Tell(FILE* file) { return int64_t(ftello(file)); }

bool TruncateStdioFile(FILE* file, uint64_t length) {
  if (fflush(file)) {
    return false;
  }
  int64_t position = Tell(file);
  if (position < 0) {
    return false;
  }
  if (ftruncate(fileno(file), length)) {
    return false;
  }
  if (uint64_t(position) > length) {
    if (!Seek(file, 0, SEEK_END)) {
      return false;
    }
  }
  return true;
}

static int removeCallback(const char* fpath, const struct stat* sb,
                          int typeflag, struct FTW* ftwbuf) {
  int rv = remove(fpath);
  return rv;
}

static uint64_t convertUnixtimeToWinFiletime(time_t unixtime) {
  // Linux uses number of seconds since 1/1/1970, and Windows uses
  // number of nanoseconds since 1/1/1601
  // So we convert Unix time to nanoseconds and then add the number of
  // nanoseconds from 1601 to 1970
  // See https://msdn.microsoft.com/en-us/library/ms724228
  uint64_t filetime = (uint64_t(unixtime) * 10000000) + 116444736000000000ULL;
  return filetime;
}

bool CreateEmptyFile(const std::filesystem::path& path) {
  int file = creat(path.c_str(), 0774);
  if (file >= 0) {
    close(file);
    return true;
  }
  return false;
}

class PosixFileHandle : public FileHandle {
 public:
  PosixFileHandle(std::filesystem::path path, int handle)
      : FileHandle(std::move(path)), handle_(handle) {}
  ~PosixFileHandle() override {
    close(handle_);
    handle_ = -1;
  }
  bool Read(size_t file_offset, void* buffer, size_t buffer_length,
            size_t* out_bytes_read) override {
    ssize_t out = pread(handle_, buffer, buffer_length, file_offset);
    if (out >= 0) {
      *out_bytes_read = out;
      return true;
    } else {
      *out_bytes_read = 0;
      return false;
    }
  }
  bool Write(size_t file_offset, const void* buffer, size_t buffer_length,
             size_t* out_bytes_written) override {
    ssize_t out = pwrite(handle_, buffer, buffer_length, file_offset);
    if (out >= 0) {
      *out_bytes_written = out;
      return true;
    } else {
      *out_bytes_written = 0;
      return false;
    }
  }
  bool SetLength(size_t length) override {
    return ftruncate(handle_, length) >= 0;
  }
  void Flush() override { fsync(handle_); }

 private:
  int handle_ = -1;
};

std::unique_ptr<FileHandle> FileHandle::OpenExisting(
    const std::filesystem::path& path, uint32_t desired_access) {
  int open_access = 0;
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
  int handle = open(path.c_str(), open_access);
  if (handle == -1) {
    // TODO: Handle error appropriately
    return nullptr;
  }
  return std::make_unique<PosixFileHandle>(path, handle);
}

bool GetInfo(const std::filesystem::path& path, FileInfo* out_info) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
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

std::vector<FileInfo> ListFiles(const std::filesystem::path& path) {
  std::vector<FileInfo> result;

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    return result;
  }

  while (auto ent = readdir(dir)) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }

    FileInfo info;

    info.name = ent->d_name;
    struct stat st;
    stat((path / info.name).c_str(), &st);
    info.create_timestamp = convertUnixtimeToWinFiletime(st.st_ctime);
    info.access_timestamp = convertUnixtimeToWinFiletime(st.st_atime);
    info.write_timestamp = convertUnixtimeToWinFiletime(st.st_mtime);
    info.path = path;

    if (S_ISDIR(st.st_mode)) {
      info.type = FileInfo::Type::kDirectory;
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size = st.st_size;
    }
    result.push_back(info);
  }
  closedir(dir);
  return result;
}

}  // namespace filesystem
}  // namespace xe
