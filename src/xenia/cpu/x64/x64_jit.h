/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_X64_X64_JIT_H_
#define XENIA_CPU_X64_X64_JIT_H_

#include <xenia/core.h>

#include <xenia/cpu/jit.h>
#include <xenia/cpu/ppc.h>
#include <xenia/cpu/sdb.h>
#include <xenia/cpu/x64/x64_emitter.h>


namespace xe {
namespace cpu {
namespace x64 {


class X64JIT : public JIT {
public:
  X64JIT(xe_memory_ref memory, sdb::SymbolTable* sym_table);
  virtual ~X64JIT();

  virtual int Setup();
  virtual void SetupGpuPointers(void* gpu_this,
                                void* gpu_read, void* gpu_write);

  virtual int InitModule(ExecModule* module);
  virtual int UninitModule(ExecModule* module);

  virtual void* GetFunctionPointer(sdb::FunctionSymbol* fn_symbol);
  virtual int Execute(xe_ppc_state_t* ppc_state,
                      sdb::FunctionSymbol* fn_symbol);

protected:
  int CheckProcessor();

  X64Emitter*     emitter_;
};


}  // namespace x64
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_X64_X64_JIT_H_
