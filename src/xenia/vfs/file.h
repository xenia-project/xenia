/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_FILE_H_
#define XENIA_VFS_FILE_H_

#include <cstdint>
#include <filesystem>
#include <span>

#include "xenia/xbox.h"

namespace xe {
namespace vfs {

class Entry;

class File {
 public:
  File(uint32_t file_access, Entry* entry)
      : file_access_(file_access), entry_(entry) {}
  virtual ~File() = default;

  virtual void Destroy() = 0;

  virtual X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                            size_t* out_bytes_read) = 0;
  virtual X_STATUS WriteSync(std::span<const uint8_t> buffer,
                             size_t byte_offset, size_t* out_bytes_written) = 0;

  // TODO: Parameters
  virtual X_STATUS ReadAsync(std::span<uint8_t> buffer, size_t byte_offset,
                             size_t* out_bytes_read) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // TODO: Parameters
  virtual X_STATUS WriteAsync(std::span<uint8_t> buffer, size_t byte_offset,
                              size_t* out_bytes_written) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  virtual X_STATUS SetLength(size_t length) { return X_STATUS_NOT_IMPLEMENTED; }
  virtual X_STATUS Rename(const std::filesystem::path file_path) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // xe::filesystem::FileAccess
  uint32_t file_access() const { return file_access_; }
  const Entry* entry() const { return entry_; }
  Entry* entry() { return entry_; }

 protected:
  // xe::filesystem::FileAccess
  uint32_t file_access_ = 0;
  Entry* entry_ = nullptr;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_FILE_H_
