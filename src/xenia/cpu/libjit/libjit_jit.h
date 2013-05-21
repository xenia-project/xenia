/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LIBJIT_LIBJIT_JIT_H_
#define XENIA_CPU_LIBJIT_LIBJIT_JIT_H_

#include <xenia/core.h>

#include <xenia/cpu/jit.h>
#include <xenia/cpu/ppc.h>
#include <xenia/cpu/sdb.h>
#include <xenia/cpu/libjit/libjit_emitter.h>

#include <jit/jit.h>


namespace xe {
namespace cpu {
namespace libjit {


class LibjitJIT : public JIT {
public:
  LibjitJIT(xe_memory_ref memory, sdb::SymbolTable* sym_table);
  virtual ~LibjitJIT();

  virtual int Setup();

  virtual int InitModule(ExecModule* module);
  virtual int UninitModule(ExecModule* module);

  virtual int Execute(xe_ppc_state_t* ppc_state,
                      sdb::FunctionSymbol* fn_symbol);

protected:
  int InjectGlobals();

  jit_context_t         context_;
  LibjitEmitter*        emitter_;
};


}  // namespace libjit
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LIBJIT_LIBJIT_JIT_H_
