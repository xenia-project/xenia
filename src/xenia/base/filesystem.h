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

// Get executable path.
std::wstring GetExecutablePath();

// Get executable folder.
std::wstring GetExecutableFolder();

// Get user folder.
std::wstring GetUserFolder();

// Canonicalizes a path, removing ..'s.
std::string CanonicalizePath(const std::string& original_path);

// Returns true of the specified path exists as either a directory or file.
bool PathExists(const std::wstring& path);

// Creates the parent folder of the specified path if needed.
// This can be used to ensure the destination path for a new file exists before
// attempting to create it.
bool CreateParentFolder(const std::wstring& path);

// Creates a folder at the specified path.
// Returns true if the path was created.
bool CreateFolder(const std::wstring& path);

// Recursively deletes the files and folders at the specified path.
// Returns true if the path was found and removed.
bool DeleteFolder(const std::wstring& path);

// Returns true if the given path exists and is a folder.
bool IsFolder(const std::wstring& path);

// Creates an empty file at the given path.
bool CreateFile(const std::wstring& path);

// Opens the file at the given path with the specified mode.
// This behaves like fopen and the returned handle can be used with stdio.
FILE* OpenFile(const std::wstring& path, const char* mode);

// Deletes the file at the given path.
// Returns true if the file was found and removed.
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

  // Set length of the file in bytes.
  virtual bool SetLength(size_t length) = 0;

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
  std::wstring path;
  size_t total_size;
  uint64_t create_timestamp;
  uint64_t access_timestamp;
  uint64_t write_timestamp;
};
bool GetInfo(const std::wstring& path, FileInfo* out_info);
std::vector<FileInfo> ListFiles(const std::wstring& path);

}  // namespace filesystem
}  // namespace xe

#endif  // XENIA_BASE_FILESYSTEM_H_
