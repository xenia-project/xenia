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
#include <xenia/core.h>

#include <xenia/kernel/objects/xfile.h>


namespace xe {
namespace kernel {
namespace fs {

class STFSContainerEntry;


class STFSContainerFile : public XFile {
public:
  STFSContainerFile(KernelState* kernel_state, uint32_t desired_access,
                    STFSContainerEntry* entry);
  virtual ~STFSContainerFile();

  virtual const char* name(void);
  virtual const char* path(void);

  virtual X_STATUS QueryInfo(XFileInfo* out_info);
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, bool restart);

protected:
  virtual X_STATUS ReadSync(
      void* buffer, size_t buffer_length, size_t byte_offset,
      size_t* out_bytes_read);

private:
  STFSContainerEntry* entry_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_STFS_CONTAINER_FILE_H_
