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

#include <xenia/core.h>

#include <vector>

#include <xenia/cpu/backend.h>
#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/thread_state.h>
#include <xenia/cpu/sdb/symbol_table.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS1(xe, ExportResolver);


namespace xe {
namespace cpu {

class JIT;


class Processor {
public:
  Processor(Emulator* emulator, Backend* backend);
  ~Processor();

  xe_memory_ref memory() const { return memory_; }
  ExportResolver* export_resolver() const { return export_resolver_; }

  int Setup();

  void AddRegisterAccessCallbacks(ppc::RegisterAccessCallbacks callbacks);

  int LoadRawBinary(const xechar_t* path, uint32_t start_address);
  int LoadXexModule(const char* name, const char* path, xe_xex2_ref xex);

  uint32_t CreateCallback(void (*callback)(void* data), void* data);

  ThreadState* AllocThread(uint32_t stack_size, uint32_t thread_state_address,
                           uint32_t thread_id);
  void DeallocThread(ThreadState* thread_state);

  int Execute(ThreadState* thread_state, uint32_t address);
  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t arg0);
  uint64_t Execute(ThreadState* thread_state, uint32_t address,
                   uint64_t arg0, uint64_t arg1);

  uint64_t ExecuteInterrupt(
      uint32_t cpu, uint32_t address, uint64_t arg0, uint64_t arg1);

  sdb::FunctionSymbol* GetFunction(uint32_t address);
  void* GetFunctionPointer(uint32_t address);

private:
  Emulator*             emulator_;
  Backend*              backend_;
  xe_memory_ref         memory_;
  ExportResolver*       export_resolver_;

  sdb::SymbolTable*     sym_table_;
  JIT*                  jit_;
  std::vector<ExecModule*> modules_;
  xe_mutex_t*           sym_lock_;

  xe_mutex_t*           interrupt_thread_lock_;
  ThreadState*          interrupt_thread_state_;
  uint32_t              interrupt_thread_block_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PROCESSOR_H_
