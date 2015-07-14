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

#include "xenia/kernel/objects/xfile.h"

typedef void* HANDLE;

namespace xe {
namespace vfs {

class HostPathEntry;

class HostPathFile : public XFile {
 public:
  HostPathFile(KernelState* kernel_state, uint32_t file_access,
               HostPathEntry* entry, HANDLE file_handle);
  ~HostPathFile() override;

 protected:
  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                     size_t byte_offset, size_t* out_bytes_written) override;

 private:
  HostPathEntry* entry_;
  HANDLE file_handle_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_HOST_PATH_FILE_H_
