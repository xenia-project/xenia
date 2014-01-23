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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/objects/xfile.h>


namespace xe {
namespace kernel {
namespace fs {

class DiscImageEntry;


class DiscImageFile : public XFile {
public:
  DiscImageFile(KernelState* kernel_state, uint32_t desired_access,
                DiscImageEntry* entry);
  virtual ~DiscImageFile();

  virtual const char* path(void) const;
  virtual const char* absolute_path(void) const;
  virtual const char* name(void) const;

  virtual X_STATUS QueryInfo(XFileInfo* out_info);
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, const char* file_name, bool restart);
  virtual X_STATUS QueryVolume(XVolumeInfo* out_info, size_t length);
  virtual X_STATUS QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length);

protected:
  virtual X_STATUS ReadSync(
      void* buffer, size_t buffer_length, size_t byte_offset,
      size_t* out_bytes_read);

private:
  DiscImageEntry* entry_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_DEVICES_DISC_IMAGE_FILE_H_
