/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_FILE_H_
#define XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_FILE_H_

#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace kernel {
namespace fs {

class DiscImageEntry;

class DiscImageFile : public XFile {
 public:
  DiscImageFile(KernelState* kernel_state, Mode desired_access,
                DiscImageEntry* entry);
  ~DiscImageFile() override;

  const std::string& path() const override;
  const std::string& name() const override;

  Device* device() const override;

  X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) override;
  X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info, size_t length,
                          const char* file_name, bool restart) override;

 protected:
  X_STATUS ReadSync(void* buffer, size_t buffer_length, size_t byte_offset,
                    size_t* out_bytes_read) override;

 private:
  DiscImageEntry* entry_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_FILE_H_
