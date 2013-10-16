/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_

#include <xenia/kernel/modules/xboxkrnl/xobject.h>

#include <xenia/kernel/xbox.h>
#include <xenia/kernel/fs/entry.h>
#include <xenia/kernel/modules/xboxkrnl/async_request.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XFile : public XObject {
public:
  XFile(KernelState* kernel_state, fs::FileEntry* entry);
  virtual ~XFile();

  X_STATUS Read(void* buffer, size_t buffer_length, size_t byte_offset,
                XAsyncRequest* request);

private:
  fs::FileEntry*  entry_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_XFILE_H_
