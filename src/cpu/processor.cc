/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/processor.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>

#include "cpu/codegen/emit.h"


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


Processor::Processor(xe_pal_ref pal, xe_memory_ref memory) {
  pal_ = xe_pal_retain(pal);
  memory_ = xe_memory_retain(memory);

  // TODO(benvanik): only do this once
  LLVMLinkInInterpreter();
  LLVMLinkInJIT();
  InitializeNativeTarget();

  // TODO(benvanik): only do this once
  codegen::RegisterEmitCategoryALU();
  codegen::RegisterEmitCategoryControl();
  codegen::RegisterEmitCategoryFPU();
  codegen::RegisterEmitCategoryMemory();
}

Processor::~Processor() {
  // Cleanup all modules.
  for (std::vector<ExecModule*>::iterator it = modules_.begin();
       it != modules_.end(); ++it) {
    delete *it;
  }

  engine_.reset();
  llvm_shutdown();

  xe_memory_release(memory_);
  xe_pal_release(pal_);
}

xe_pal_ref Processor::pal() {
  return xe_pal_retain(pal_);
}

xe_memory_ref Processor::memory() {
  return xe_memory_retain(memory_);
}

int Processor::Setup() {
  XEASSERTNULL(engine_);

  if (!llvm_start_multithreaded()) {
    return 1;
  }

  dummy_context_ = auto_ptr<LLVMContext>(new LLVMContext());
  Module* dummy_module = new Module("dummy", *dummy_context_.get());

  std::string error_message;
  engine_ = shared_ptr<ExecutionEngine>(
      ExecutionEngine::create(dummy_module, false, &error_message,
                              CodeGenOpt::Aggressive));
  if (!engine_) {
    return 1;
  }

  return 0;
}

int Processor::PrepareModule(UserModule* user_module,
                             shared_ptr<ExportResolver> export_resolver) {
  ExecModule* exec_module = new ExecModule(
      memory_, export_resolver, user_module, engine_);

  if (exec_module->Prepare()) {
    delete exec_module;
    return 1;
  }

  modules_.push_back(exec_module);

  //user_module->Dump(export_resolver.get());
  exec_module->Dump();

  return 0;
}

int Processor::Execute(uint32_t address) {
  // TODO(benvanik): implement execute.
  return 0;
}

uint32_t Processor::CreateCallback(void (*callback)(void* data), void* data) {
  // TODO(benvanik): implement callback creation.
  return 0;
}
