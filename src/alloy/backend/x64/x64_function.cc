/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_function.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::runtime;


X64Function::X64Function(FunctionInfo* symbol_info) :
    machine_code_(0), code_size_(0),
    GuestFunction(symbol_info) {
}

X64Function::~X64Function() {
}

void X64Function::Setup(void* machine_code, size_t code_size) {
  machine_code_ = machine_code;
  code_size_ = code_size;
}

int X64Function::AddBreakpointImpl(Breakpoint* breakpoint) {
  return 0;
}

int X64Function::RemoveBreakpointImpl(Breakpoint* breakpoint) {
  return 0;
}

int X64Function::CallImpl(ThreadState* thread_state, uint64_t return_address) {
  //typedef void(*call_t)(ThreadState* thread_state, uint64_t return_address);
  //((call_t)machine_code_)(thread_state, return_address);
  typedef void(*call_t)(ThreadState* thread_state, uint8_t* membase);
  ((call_t)machine_code_)(thread_state, thread_state->memory()->membase());
  return 0;
}
