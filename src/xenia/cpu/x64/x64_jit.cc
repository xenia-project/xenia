/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_jit.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/sdb.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::cpu::x64;


X64JIT::X64JIT(xe_memory_ref memory, SymbolTable* sym_table) :
    JIT(memory, sym_table),
    context_(NULL), emitter_(NULL) {
}

X64JIT::~X64JIT() {
  delete emitter_;
  if (context_) {
    jit_context_destroy(context_);
  }
}

int X64JIT::Setup() {
  int result_code = 1;

  // Shared libjit context.
  context_ = jit_context_create();
  XEEXPECTNOTNULL(context_);

  // Create the emitter used to generate functions.
  emitter_ = new X64Emitter(memory_, context_);

  // Inject global functions/variables/etc.
  XEEXPECTZERO(InjectGlobals());

  result_code = 0;
XECLEANUP:
  return result_code;
}

int X64JIT::InjectGlobals() {
  return 0;
}

int X64JIT::InitModule(ExecModule* module) {
  return 0;
}

int X64JIT::UninitModule(ExecModule* module) {
  return 0;
}

int X64JIT::Execute(xe_ppc_state_t* ppc_state, FunctionSymbol* fn_symbol) {
  XELOGCPU("Execute(%.8X): %s...", fn_symbol->start_address, fn_symbol->name());

  // Check function.
  jit_function_t jit_fn = (jit_function_t)fn_symbol->impl_value;
  if (!jit_fn) {
    // Function hasn't been prepped yet - prep it.
    if (emitter_->PrepareFunction(fn_symbol)) {
      XELOGCPU("Execute(%.8X): unable to make function %s",
          fn_symbol->start_address, fn_symbol->name());
      return 1;
    }
    jit_fn = (jit_function_t)fn_symbol->impl_value;
    XEASSERTNOTNULL(jit_fn);
  }

  // Call into the function. This will compile it if needed.
  jit_nuint lr = ppc_state->lr;
  void* args[] = {&ppc_state, &lr};
  uint64_t return_value;
  int apply_result = jit_function_apply(jit_fn, (void**)&args, &return_value);
  if (!apply_result) {
    XELOGCPU("Execute(%.8X): apply failed with %d",
        fn_symbol->start_address, apply_result);
    return 1;
  }

  return 0;
}
