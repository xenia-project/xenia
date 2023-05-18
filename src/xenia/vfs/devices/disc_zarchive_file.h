/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_ZARCHIVE_FILE_H_
#define XENIA_VFS_DEVICES_DISC_ZARCHIVE_FILE_H_

#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {

class DiscZarchiveEntry;

class DiscZarchiveFile : public File {
 public:
  DiscZarchiveFile(uint32_t file_access, DiscZarchiveEntry* entry);
  ~DiscZarchiveFile() override;

  void Destroy() override;

  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                     size_t byte_offset, size_t* out_bytes_written) override {
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t length) override { return X_STATUS_ACCESS_DENIED; }

 private:
  DiscZarchiveEntry* entry_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_ZARCHIVE_FILE_H_
