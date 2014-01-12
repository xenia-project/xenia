/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_STATE_H_
#define XENIA_KERNEL_KERNEL_STATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/xbox.h>
#include <xenia/kernel/object_table.h>
#include <xenia/kernel/fs/filesystem.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS2(xe, kernel, Dispatcher);
XEDECLARECLASS2(xe, kernel, XModule);
XEDECLARECLASS2(xe, kernel, XThread);
XEDECLARECLASS2(xe, kernel, XUserModule);
XEDECLARECLASS3(xe, kernel, fs, FileSystem);


namespace xe {
namespace kernel {


class KernelState {
public:
  KernelState(Emulator* emulator);
  ~KernelState();

  static KernelState* shared();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }
  fs::FileSystem* file_system() const { return file_system_; }

  Dispatcher* dispatcher() const { return dispatcher_; }

  ObjectTable* object_table() const { return object_table_; }

  XModule* GetModule(const char* name);
  XUserModule* GetExecutableModule();
  void SetExecutableModule(XUserModule* module);

  void RegisterThread(XThread* thread);
  void UnregisterThread(XThread* thread);
  XThread* GetThreadByID(uint32_t thread_id);

private:
  Emulator*       emulator_;
  Memory*         memory_;
  cpu::Processor* processor_;
  fs::FileSystem* file_system_;

  Dispatcher*     dispatcher_;

  ObjectTable*    object_table_;
  xe_mutex_t*     object_mutex_;
  std::unordered_map<uint32_t, XThread*> threads_by_id_;

  XUserModule*    executable_module_;

  friend class XObject;
};


// This is a global object initialized with the KernelState ctor.
// It references the current kernel state object that all kernel methods should
// be using to stash their variables.
// This sucks, but meh.
extern KernelState* shared_kernel_state_;


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_KERNEL_STATE_H_
