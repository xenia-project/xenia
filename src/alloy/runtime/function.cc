/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/function.h>

#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::runtime;


Function::Function(Type type, uint64_t address) :
    type_(type), address_(address) {
}

Function::~Function() {
}

int Function::Call(ThreadState* thread_state) {
  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }
  int result = CallImpl(thread_state);
  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }
  return result;
}

ExternFunction::ExternFunction(
    uint64_t address, Handler handler, void* arg0, void* arg1) :
    handler_(handler), arg0_(arg0), arg1_(arg1),
    Function(Function::EXTERN_FUNCTION, address) {
}

ExternFunction::~ExternFunction() {
}

int ExternFunction::CallImpl(ThreadState* thread_state) {
  if (!handler_) {
    XELOGW("undefined extern call to %.8X", address());
    return 0;
  }
  handler_(thread_state->raw_context(), arg0_, arg1_);
  return 0;
}

GuestFunction::GuestFunction(FunctionInfo* symbol_info) :
    symbol_info_(symbol_info),
    Function(Function::USER_FUNCTION, symbol_info->address()) {
}

GuestFunction::~GuestFunction() {
}
