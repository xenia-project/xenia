/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_FILE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_FILE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/modules/xboxkrnl/objects/xfile.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {
namespace fs {

class HostPathEntry;


class HostPathFile : public XFile {
public:
  HostPathFile(KernelState* kernel_state, HostPathEntry* entry);
  virtual ~HostPathFile();

private:
  HostPathEntry* entry_;
};


}  // namespace fs
}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_FS_DEVICES_HOST_PATH_FILE_H_
