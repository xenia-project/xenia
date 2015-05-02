/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_RUNTIME_H_
#define XENIA_CPU_RUNTIME_H_

#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/debugger.h"
#include "xenia/cpu/entry_table.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/export_resolver.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

class Runtime {
 public:
  Runtime(Memory* memory, ExportResolver* export_resolver,
          uint32_t debug_info_flags, uint32_t trace_flags);
  ~Runtime();

  Memory* memory() const { return memory_; }
  Debugger* debugger() const { return debugger_.get(); }
  frontend::PPCFrontend* frontend() const { return frontend_.get(); }
  backend::Backend* backend() const { return backend_.get(); }
  ExportResolver* export_resolver() const { return export_resolver_; }

  int Initialize(std::unique_ptr<backend::Backend> backend = 0);

  int AddModule(std::unique_ptr<Module> module);
  Module* GetModule(const char* name);
  Module* GetModule(const std::string& name) { return GetModule(name.c_str()); }
  std::vector<Module*> GetModules();

  Module* builtin_module() const { return builtin_module_; }
  FunctionInfo* DefineBuiltin(const std::string& name,
                              FunctionInfo::ExternHandler handler, void* arg0,
                              void* arg1);

  std::vector<Function*> FindFunctionsWithAddress(uint32_t address);

  int LookupFunctionInfo(uint32_t address, FunctionInfo** out_symbol_info);
  int LookupFunctionInfo(Module* module, uint32_t address,
                         FunctionInfo** out_symbol_info);
  int ResolveFunction(uint32_t address, Function** out_function);

  // uint32_t CreateCallback(void (*callback)(void* data), void* data);

 private:
  int DemandFunction(FunctionInfo* symbol_info, Function** out_function);

  Memory* memory_;

  uint32_t debug_info_flags_;
  uint32_t trace_flags_;

  std::unique_ptr<Debugger> debugger_;

  std::unique_ptr<frontend::PPCFrontend> frontend_;
  std::unique_ptr<backend::Backend> backend_;
  ExportResolver* export_resolver_;

  EntryTable entry_table_;
  std::mutex modules_lock_;
  std::vector<std::unique_ptr<Module>> modules_;
  Module* builtin_module_;
  uint32_t next_builtin_address_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_RUNTIME_H_
