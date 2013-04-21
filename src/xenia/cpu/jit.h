/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_JIT_H_
#define XENIA_CPU_JIT_H_

#include <xenia/core.h>

#include <xenia/cpu/function_table.h>
#include <xenia/cpu/ppc.h>
#include <xenia/cpu/sdb.h>


namespace xe {
namespace cpu {


class ExecModule;


class JIT {
public:
  virtual ~JIT() {
    xe_memory_release(memory_);
  }

  virtual int Setup() = 0;

  virtual int InitModule(ExecModule* module) = 0;
  virtual int UninitModule(ExecModule* module) = 0;

  virtual FunctionPointer GenerateFunction(sdb::FunctionSymbol* symbol) = 0;

protected:
  JIT(xe_memory_ref memory, FunctionTable* fn_table) {
    memory_ = xe_memory_retain(memory);
    fn_table_ = fn_table;
  }

  xe_memory_ref   memory_;
  FunctionTable*  fn_table_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_JIT_H_
