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

#include <mutex>
#include <vector>

#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/entry_table.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/debugger.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

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
  frontend::PPCFrontend* frontend() const { return frontend_.get(); }
  backend::Backend* backend() const { return backend_.get(); }
  ExportResolver* export_resolver() const { return export_resolver_; }

  bool Setup();

  bool AddModule(std::unique_ptr<Module> module);
  Module* GetModule(const char* name);
  Module* GetModule(const std::string& name) { return GetModule(name.c_str()); }
  std::vector<Module*> GetModules();

  Module* builtin_module() const { return builtin_module_; }
  FunctionInfo* DefineBuiltin(const std::string& name,
                              FunctionInfo::ExternHandler handler, void* arg0,
                              void* arg1);

  std::vector<Function*> FindFunctionsWithAddress(uint32_t address);

  bool LookupFunctionInfo(uint32_t address, FunctionInfo** out_symbol_info);
  bool LookupFunctionInfo(Module* module, uint32_t address,
                          FunctionInfo** out_symbol_info);
  bool ResolveFunction(uint32_t address, Function** out_function);

  bool Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[],
                   size_t arg_count);

  Irql RaiseIrql(Irql new_value);
  void LowerIrql(Irql old_value);

  uint64_t ExecuteInterrupt(uint32_t cpu, uint32_t address, uint64_t args[],
                            size_t arg_count);

 private:
  bool DemandFunction(FunctionInfo* symbol_info, Function** out_function);

  Memory* memory_;
  debug::Debugger* debugger_;

  uint32_t debug_info_flags_;

  std::unique_ptr<frontend::PPCFrontend> frontend_;
  std::unique_ptr<backend::Backend> backend_;
  ExportResolver* export_resolver_;

  EntryTable entry_table_;
  std::mutex modules_lock_;
  std::vector<std::unique_ptr<Module>> modules_;
  Module* builtin_module_;
  uint32_t next_builtin_address_;

  Irql irql_;
  std::mutex interrupt_thread_lock_;
  ThreadState* interrupt_thread_state_;
  uint32_t interrupt_thread_block_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PROCESSOR_H_
