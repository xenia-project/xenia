/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_HOST_PATH_FILE_H_
#define XENIA_VFS_DEVICES_HOST_PATH_FILE_H_

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {

class HostPathEntry;

class HostPathFile : public File {
 public:
  HostPathFile(uint32_t file_access, HostPathEntry* entry,
               std::unique_ptr<xe::filesystem::FileHandle> file_handle);
  ~HostPathFile() override;

  void Destroy() override;

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                     size_t* out_bytes_written) override;
  X_STATUS SetLength(size_t length) override;

 private:
  std::unique_ptr<xe::filesystem::FileHandle> file_handle_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_HOST_PATH_FILE_H_
