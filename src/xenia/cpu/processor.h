/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PROCESSOR_H_
#define XENIA_CPU_PROCESSOR_H_

#include <memory>
#include <string>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/entry_table.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/memory.h"

namespace xe {
namespace debug {
class Debugger;
}  // namespace debug
}  // namespace xe

namespace xe {
namespace cpu {

class StackWalker;
class ThreadState;
class XexModule;

enum class Irql : uint32_t {
  PASSIVE = 0,
  APC = 1,
  DISPATCH = 2,
  DPC = 3,
};

class Processor {
 public:
  Processor(Memory* memory, ExportResolver* export_resolver,
            debug::Debugger* debugger);
  ~Processor();

  Memory* memory() const { return memory_; }
  debug::Debugger* debugger() const { return debugger_; }
  StackWalker* stack_walker() const { return stack_walker_.get(); }
  frontend::PPCFrontend* frontend() const { return frontend_.get(); }
  backend::Backend* backend() const { return backend_.get(); }
  ExportResolver* export_resolver() const { return export_resolver_; }

  bool Setup();

  void set_debug_info_flags(uint32_t debug_info_flags) {
    debug_info_flags_ = debug_info_flags;
  }

  bool AddModule(std::unique_ptr<Module> module);
  Module* GetModule(const char* name);
  Module* GetModule(const std::string& name) { return GetModule(name.c_str()); }
  std::vector<Module*> GetModules();

  Module* builtin_module() const { return builtin_module_; }
  Function* DefineBuiltin(const std::string& name,
                          BuiltinFunction::Handler handler, void* arg0,
                          void* arg1);

  Function* QueryFunction(uint32_t address);
  std::vector<Function*> FindFunctionsWithAddress(uint32_t address);

  Function* LookupFunction(uint32_t address);
  Function* LookupFunction(Module* module, uint32_t address);
  Function* ResolveFunction(uint32_t address);

  bool Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[],
                   size_t arg_count);
  uint64_t ExecuteInterrupt(ThreadState* thread_state, uint32_t address,
                            uint64_t args[], size_t arg_count);

  Irql RaiseIrql(Irql new_value);
  void LowerIrql(Irql old_value);

 private:
  bool DemandFunction(Function* function);

  Memory* memory_ = nullptr;
  debug::Debugger* debugger_ = nullptr;
  std::unique_ptr<StackWalker> stack_walker_;

  uint32_t debug_info_flags_ = 0;

  std::unique_ptr<frontend::PPCFrontend> frontend_;
  std::unique_ptr<backend::Backend> backend_;
  ExportResolver* export_resolver_ = nullptr;

  EntryTable entry_table_;
  xe::global_critical_region global_critical_region_;
  std::vector<std::unique_ptr<Module>> modules_;
  Module* builtin_module_ = nullptr;
  uint32_t next_builtin_address_ = 0xFFFF0000u;

  Irql irql_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PROCESSOR_H_
