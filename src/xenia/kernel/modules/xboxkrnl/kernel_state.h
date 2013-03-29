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
#include <xenia/kernel/fs/filesystem.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XObject;
class XModule;
class XThread;


class KernelState {
public:
  KernelState(Runtime* runtime);
  ~KernelState();

  Runtime* runtime();
  xe_memory_ref memory();
  cpu::Processor* processor();
  fs::FileSystem* filesystem();

  XObject* GetObject(X_HANDLE handle);

  XModule* GetModule(const char* name);
  XModule* GetExecutableModule();
  void SetExecutableModule(XModule* module);

private:
  X_HANDLE InsertObject(XObject* obj);
  void RemoveObject(XObject* obj);

  Runtime*      runtime_;
  xe_memory_ref memory_;
  shared_ptr<cpu::Processor> processor_;
  shared_ptr<fs::FileSystem> filesystem_;

  XModule*      executable_module_;

  xe_mutex_t* objects_mutex_;
  X_HANDLE next_handle_;
  std::tr1::unordered_map<X_HANDLE, XObject*> objects_;
  std::tr1::unordered_map<X_HANDLE, XModule*> modules_;
  std::tr1::unordered_map<X_HANDLE, XThread*> threads_;

  friend class XObject;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_KERNEL_STATE_H_
