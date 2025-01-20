/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

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
  char buff[FILENAME_MAX] = "";
  readlink("/proc/self/exe", buff, FILENAME_MAX);
  std::string s(buff);
  return s;
}

std::filesystem::path GetExecutableFolder() {
  return GetExecutablePath().parent_path();
}

std::filesystem::path GetUserFolder() {
  // get preferred data home
  char* home = std::getenv("XDG_DATA_HOME");
  if (home) {
    return std::string(home);
  }

  // if XDG_DATA_HOME not set, fallback to HOME directory
  home = std::getenv("HOME");

  // if HOME not set, fall back to this
  if (home == NULL) {
    struct passwd pw1;
    struct passwd* pw;
    char buf[4096];  // could potentionally lower this
    getpwuid_r(getuid(), &pw1, buf, sizeof(buf), &pw);
    assert(&pw1 == pw);  // sanity check
    home = pw->pw_dir;
  }

  return std::filesystem::path(home) / ".local" / "share";
}

FILE* OpenFile(const std::filesystem::path& path, const std::string_view mode) {
  return fopen(path.c_str(), std::string(mode).c_str());
}

bool Seek(FILE* file, int64_t offset, int origin) {
  return fseeko64(file, off64_t(offset), origin) == 0;
}

int64_t Tell(FILE* file) { return int64_t(ftello64(file)); }

bool TruncateStdioFile(FILE* file, uint64_t length) {
  if (fflush(file)) {
    return false;
  }
  int64_t position = Tell(file);
  if (position < 0) {
    return false;
  }
  if (ftruncate64(fileno(file), off64_t(length))) {
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
  // so we convert linux time to nanoseconds and then add the number of
  // nanoseconds from 1601 to 1970
  // see https://msdn.microsoft.com/en-us/library/ms724228
  uint64_t filetime = (unixtime * 10000000) + 116444736000000000;
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
    *out_bytes_read = out;
    return out >= 0 ? true : false;
  }
  bool Write(size_t file_offset, const void* buffer, size_t buffer_length,
             size_t* out_bytes_written) override {
    ssize_t out = pwrite(handle_, buffer, buffer_length, file_offset);
    *out_bytes_written = out;
    return out >= 0 ? true : false;
  }
  bool SetLength(size_t length) override {
    return ftruncate(handle_, length) >= 0 ? true : false;
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
    // TODO(benvanik): pick correct response.
    return nullptr;
  }
  return std::make_unique<PosixFileHandle>(path, handle);
}

std::optional<FileInfo> GetInfo(const std::filesystem::path& path) {
  FileInfo info{};
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      info.type = FileInfo::Type::kDirectory;
      // On Linux st.st_size can have non-zero size (generally 4096) so make 0
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size = st.st_size;
    }
    info.path = path.parent_path();
    info.name = path.filename();
    info.create_timestamp = convertUnixtimeToWinFiletime(st.st_ctime);
    info.access_timestamp = convertUnixtimeToWinFiletime(st.st_atime);
    info.write_timestamp = convertUnixtimeToWinFiletime(st.st_mtime);
    return std::move(info);
  }
  return {};
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
    if (ent->d_type == DT_DIR) {
      info.type = FileInfo::Type::kDirectory;
      info.total_size = 0;
    } else {
      info.type = FileInfo::Type::kFile;
      info.total_size = st.st_size;
    }
    result.push_back(info);
  }
  closedir(dir);
  return std::move(result);
}

bool SetAttributes(const std::filesystem::path& path, uint64_t attributes) {
  return false;
}

}  // namespace filesystem
}  // namespace xe
