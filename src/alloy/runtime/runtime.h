/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_RUNTIME_H_
#define ALLOY_RUNTIME_RUNTIME_H_

#include <alloy/core.h>
#include <alloy/memory.h>
#include <alloy/backend/backend.h>
#include <alloy/frontend/frontend.h>
#include <alloy/runtime/entry_table.h>
#include <alloy/runtime/module.h>
#include <alloy/runtime/register_access.h>
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/thread_state.h>


namespace alloy {
namespace runtime {


class Runtime {
public:
  Runtime(Memory* memory);
  virtual ~Runtime();

  Memory* memory() const { return memory_; }
  frontend::Frontend* frontend() const { return frontend_; }
  backend::Backend* backend() const { return backend_; }

  int Initialize(frontend::Frontend* frontend, backend::Backend* backend = 0);

  int AddModule(Module* module);

  int LookupFunctionInfo(uint64_t address, FunctionInfo** out_symbol_info);
  int ResolveFunction(uint64_t address, Function** out_function);

  void AddRegisterAccessCallbacks(RegisterAccessCallbacks callbacks);

  //uint32_t CreateCallback(void (*callback)(void* data), void* data);

private:
  int DemandFunction(FunctionInfo* symbol_info, Function** out_function);

protected:
  Memory*             memory_;

  frontend::Frontend* frontend_;
  backend::Backend*   backend_;

  EntryTable          entry_table_;
  Mutex*              modules_lock_;
  typedef std::vector<Module*> ModuleList;
  ModuleList          modules_;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_RUNTIME_H_
