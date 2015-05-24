/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_HOST_PATH_FILE_H_
#define XENIA_KERNEL_FS_DEVICES_HOST_PATH_FILE_H_

#include <string>

#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace kernel {
namespace fs {

class HostPathEntry;

class HostPathFile : public XFile {
 public:
  HostPathFile(KernelState* kernel_state, Mode mode, HostPathEntry* entry,
               HANDLE file_handle);
  ~HostPathFile() override;

  const std::string& path() const override;
  const std::string& name() const override;

  Device* device() const override;

  X_STATUS QueryInfo(XFileInfo* out_info) override;
  X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                          const char* file_name, bool restart) override;
  X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length) override;
  X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info,
                                     size_t length) override;

 protected:
  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(const void* buffer, size_t buffer_length,
                     size_t byte_offset, size_t* out_bytes_written) override;

 private:
  HostPathEntry* entry_;
  HANDLE file_handle_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_HOST_PATH_FILE_H_
