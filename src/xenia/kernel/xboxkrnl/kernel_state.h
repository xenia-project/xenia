/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_KERNEL_STATE_H_
#define XENIA_KERNEL_XBOXKRNL_KERNEL_STATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/xbox.h>
#include <xenia/kernel/kernel_module.h>
#include <xenia/kernel/xboxkrnl/object_table.h>
#include <xenia/kernel/xboxkrnl/fs/filesystem.h>


XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS3(xe, kernel, fs, FileSystem);
XEDECLARECLASS3(xe, kernel, xboxkrnl, XModule);


namespace xe {
namespace kernel {
namespace xboxkrnl {


class KernelState {
public:
  KernelState(Emulator* emulator);
  ~KernelState();

  static KernelState* shared();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  fs::FileSystem* file_system() const { return file_system_; }

  ObjectTable* object_table() const { return object_table_; }

  XModule* GetModule(const char* name);
  XModule* GetExecutableModule();
  void SetExecutableModule(XModule* module);

private:
  Emulator*       emulator_;
  Memory*         memory_;
  cpu::Processor* processor_;
  fs::FileSystem* file_system_;

  ObjectTable*    object_table_;
  xe_mutex_t*     object_mutex_;

  XModule*        executable_module_;

  friend class XObject;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_KERNEL_STATE_H_
