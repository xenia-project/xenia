/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_KERNEL_STATE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_KERNEL_STATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/kernel_module.h>
#include <xenia/kernel/xbox.h>
#include <xenia/kernel/modules/xboxkrnl/object_table.h>
#include <xenia/kernel/modules/xboxkrnl/fs/filesystem.h>


namespace xe {
namespace cpu {
class Processor;
}
}


namespace xe {
namespace kernel {
namespace xboxkrnl {

class XModule;
namespace fs {
class FileSystem;
}


class KernelState {
public:
  KernelState(Runtime* runtime);
  ~KernelState();

  Runtime* runtime();
  xe_memory_ref memory();
  cpu::Processor* processor();
  fs::FileSystem* filesystem();

  ObjectTable* object_table() const;

  XModule* GetModule(const char* name);
  XModule* GetExecutableModule();
  void SetExecutableModule(XModule* module);

private:
  Runtime*      runtime_;
  xe_memory_ref memory_;
  shared_ptr<cpu::Processor> processor_;
  shared_ptr<fs::FileSystem> filesystem_;

  ObjectTable*  object_table_;
  xe_mutex_t*   object_mutex_;

  XModule*      executable_module_;

  friend class XObject;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_KERNEL_STATE_H_
