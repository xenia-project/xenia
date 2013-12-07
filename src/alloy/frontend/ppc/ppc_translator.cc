/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_translator.h>

#include <alloy/compiler/passes.h>
#include <alloy/frontend/tracing.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/frontend/ppc/ppc_function_builder.h>
#include <alloy/frontend/ppc/ppc_scanner.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::compiler;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::hir;
using namespace alloy::runtime;


PPCTranslator::PPCTranslator(PPCFrontend* frontend) :
    frontend_(frontend) {
  scanner_ = new PPCScanner(frontend);
  builder_ = new PPCFunctionBuilder(frontend);

  compiler_ = new Compiler();

  // TODO(benvanik): passes in a sensible order/etc.
  compiler_->AddPass(new passes::Mem2RegPass());

  Backend* backend = frontend->runtime()->backend();
  assembler_ = backend->CreateAssembler();
}

PPCTranslator::~PPCTranslator() {
  delete assembler_;
  delete compiler_;
  delete builder_;
  delete scanner_;
}

int PPCTranslator::Translate(
    FunctionInfo* symbol_info,
    Function** out_function) {
  char* pre_ir = NULL;
  char* post_ir = NULL;

  // Scan the function to find its extents. We only need to do this if we
  // haven't already been provided with them from some other source.
  if (!symbol_info->has_end_address()) {
    // TODO(benvanik): find a way to remove the need for the scan. A fixup
    //     scheme acting on branches could go back and modify calls to branches
    //     if they are within the extents.
    int result = scanner_->FindExtents(symbol_info);
    if (result) {
      return result;
    }
  }

  // Emit function.
  int result = builder_->Emit(symbol_info);
  XEEXPECTZERO(result);

  if (true) {
    builder_->Dump(&string_buffer_);
    pre_ir = string_buffer_.ToString();
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  result = compiler_->Compile(builder_);
  XEEXPECTZERO(result);

  if (true) {
    builder_->Dump(&string_buffer_);
    post_ir = string_buffer_.ToString();
    string_buffer_.Reset();
  }

  // Assemble to backend machine code.
  result = assembler_->Assemble(symbol_info, builder_, out_function);
  XEEXPECTZERO(result);

  result = 0;

XECLEANUP:
  if (pre_ir) xe_free(pre_ir);
  if (post_ir) xe_free(post_ir);
  builder_->Reset();
  compiler_->Reset();
  assembler_->Reset();
  string_buffer_.Reset();
  return result;
};
