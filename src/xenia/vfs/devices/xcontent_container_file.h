/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_CONTAINER_FILE_H_
#define XENIA_VFS_DEVICES_XCONTENT_CONTAINER_FILE_H_

#include "xenia/vfs/file.h"

#include "xenia/xbox.h"

namespace xe {
namespace vfs {

class XContentContainerEntry;

class XContentContainerFile : public File {
 public:
  XContentContainerFile(uint32_t file_access, XContentContainerEntry* entry);
  ~XContentContainerFile() override;

  void Destroy() override;

  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                     size_t byte_offset, size_t* out_bytes_written) override {
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t length) override { return X_STATUS_ACCESS_DENIED; }

 private:
  XContentContainerEntry* entry_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_XCONTENT_CONTAINER_FILE_H_
