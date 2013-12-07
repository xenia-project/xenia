/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_function.h>

#include <alloy/backend/tracing.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::ivm;
using namespace alloy::runtime;


IVMFunction::IVMFunction(FunctionInfo* symbol_info) :
    register_count_(0), intcode_count_(0), intcodes_(0),
    GuestFunction(symbol_info) {
}

IVMFunction::~IVMFunction() {
  xe_free(intcodes_);
}

void IVMFunction::Setup(TranslationContext& ctx) {
  register_count_ = ctx.register_count;
  intcode_count_ = ctx.intcode_count;
  intcodes_ = (IntCode*)ctx.intcode_arena->CloneContents();
}

int IVMFunction::CallImpl(ThreadState* thread_state) {
  // Setup register file on stack.
  size_t register_file_size = register_count_ * sizeof(Register);
  Register* register_file = (Register*)alloca(register_file_size);

  IntCodeState ics;
  ics.rf = register_file;
  ics.context = (uint8_t*)thread_state->raw_context();
  ics.membase = thread_state->memory()->membase();
  ics.did_carry = 0;
  ics.thread_state = thread_state;
  ics.return_address = 0xBEBEBEBE;

  // TODO(benvanik): DID_CARRY -- need HIR to set a OPCODE_FLAG_SET_CARRY
  //                 or something so the fns can set an ics flag.

  uint32_t ia = 0;
  while (true) {
    const IntCode* i = &intcodes_[ia];
    uint32_t new_ia = i->intcode_fn(ics, i);
    if (new_ia == IA_NEXT) {
      ia++;
    } else if (new_ia == IA_RETURN) {
      break;
    } else {
      ia = new_ia;
    }
  }

  return 0;
}
