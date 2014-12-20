/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_FILE_H_
#define XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_FILE_H_

#include <xenia/common.h>
#include <xenia/kernel/objects/xfile.h>

namespace xe {
namespace kernel {
namespace fs {

class STFSContainerEntry;

class STFSContainerFile : public XFile {
 public:
  STFSContainerFile(KernelState* kernel_state, Mode mode,
                    STFSContainerEntry* entry);
  ~STFSContainerFile() override;

  const std::string& path() const override;
  const std::string& absolute_path() const override;
  const std::string& name() const override;

  X_STATUS QueryInfo(XFileInfo* out_info) override;
  X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                          const char* file_name, bool restart) override;
  X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length) override;
  X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info,
                                     size_t length) override;

 protected:
  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;

 private:
  STFSContainerEntry* entry_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_FILE_H_
