/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_FILE_H_
#define XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_FILE_H_

#include "xenia/vfs/devices/xcontent_container_file.h"

namespace xe {
namespace vfs {

class SvodContainerEntry;

class SvodContainerFile : public XContentContainerFile {
 public:
  SvodContainerFile(uint32_t file_access, SvodContainerEntry* entry);
  ~SvodContainerFile() override;

  void Destroy() override;

  X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                     size_t* out_bytes_written) override {
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t length) override { return X_STATUS_ACCESS_DENIED; }

 private:
  size_t Read(std::span<uint8_t> buffer, size_t offset,
              size_t record_file) override;

  SvodContainerEntry* entry_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_FILE_H_
