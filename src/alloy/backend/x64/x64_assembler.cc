/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_assembler.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_function.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::hir;
using namespace alloy::runtime;


X64Assembler::X64Assembler(X64Backend* backend) :
    Assembler(backend) {
}

X64Assembler::~X64Assembler() {
  alloy::tracing::WriteEvent(EventType::AssemblerDeinit({
  }));
}

int X64Assembler::Initialize() {
  int result = Assembler::Initialize();
  if (result) {
    return result;
  }

  alloy::tracing::WriteEvent(EventType::AssemblerInit({
  }));

  return result;
}

void X64Assembler::Reset() {
  Assembler::Reset();
}

int X64Assembler::Assemble(
    FunctionInfo* symbol_info, HIRBuilder* builder,
    DebugInfo* debug_info, Function** out_function) {
  X64Function* fn = new X64Function(symbol_info);
  fn->set_debug_info(debug_info);

  *out_function = fn;
  return 0;
}
