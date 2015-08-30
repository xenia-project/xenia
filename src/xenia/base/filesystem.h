/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_FILESYSTEM_H_
#define XENIA_BASE_FILESYSTEM_H_

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/string.h"

namespace xe {
namespace filesystem {

std::string CanonicalizePath(const std::string& original_path);

bool PathExists(const std::wstring& path);

bool CreateParentFolder(const std::wstring& path);
bool CreateFolder(const std::wstring& path);
bool DeleteFolder(const std::wstring& path);
bool IsFolder(const std::wstring& path);

FILE* OpenFile(const std::wstring& path, const char* mode);
bool DeleteFile(const std::wstring& path);

struct FileAccess {
  // Implies kFileReadData.
  static const uint32_t kGenericRead = 0x80000000;
  // Implies kFileWriteData.
  static const uint32_t kGenericWrite = 0x40000000;
  static const uint32_t kGenericExecute = 0x20000000;
  static const uint32_t kGenericAll = 0x10000000;
  static const uint32_t kFileReadData = 0x00000001;
  static const uint32_t kFileWriteData = 0x00000002;
  static const uint32_t kFileAppendData = 0x00000004;
};

class FileHandle {
 public:
  // Opens the file, failing if it doesn't exist.
  // The desired_access bitmask denotes the permissions on the file.
  static std::unique_ptr<FileHandle> OpenExisting(std::wstring path,
                                                  uint32_t desired_access);

  virtual ~FileHandle() = default;

  std::wstring path() const { return path_; }

  // Reads the requested number of bytes from the file starting at the given
  // offset. The total number of bytes read is returned only if the complete
  // read succeeds.
  virtual bool Read(size_t file_offset, void* buffer, size_t buffer_length,
                    size_t* out_bytes_read) = 0;

  // Writes the given buffer to the file starting at the given offset.
  // The total number of bytes written is returned only if the complete
  // write succeeds.
  virtual bool Write(size_t file_offset, const void* buffer,
                     size_t buffer_length, size_t* out_bytes_written) = 0;

  // Flushes any pending write buffers to the underlying filesystem.
  virtual void Flush() = 0;

 protected:
  explicit FileHandle(std::wstring path) : path_(std::move(path)) {}

  std::wstring path_;
};

struct FileInfo {
  enum class Type {
    kFile,
    kDirectory,
  };
  Type type;
  std::wstring name;
  size_t total_size;
  uint64_t create_timestamp;
  uint64_t access_timestamp;
  uint64_t write_timestamp;
};
bool GetInfo(const std::wstring& path, FileInfo* out_info);
std::vector<FileInfo> ListFiles(const std::wstring& path);

class WildcardFlags {
 public:
  bool FromStart : 1, ToEnd : 1;

  WildcardFlags();
  WildcardFlags(bool start, bool end);

  static WildcardFlags FIRST;
  static WildcardFlags LAST;
};

class WildcardRule {
 public:
  WildcardRule(const std::string& str_match, const WildcardFlags& flags);
  bool Check(const std::string& str_lower,
             std::string::size_type* offset) const;

 private:
  std::string match;
  WildcardFlags rules;
};

class WildcardEngine {
 public:
  void SetRule(const std::string& pattern);

  // Always ignoring case
  bool Match(const std::string& str) const;

 private:
  std::vector<WildcardRule> rules;
  void PreparePattern(const std::string& pattern);
};

}  // namespace filesystem
}  // namespace xe

#endif  // XENIA_BASE_FILESYSTEM_H_
