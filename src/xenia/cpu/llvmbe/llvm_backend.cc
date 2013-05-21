/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvmbe/llvm_backend.h>

#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>

#include <xenia/cpu/llvmbe/emit.h>
#include <xenia/cpu/llvmbe/llvm_jit.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::llvmbe;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    LLVMLinkInInterpreter();
    LLVMLinkInJIT();
    InitializeNativeTarget();

    llvm_start_multithreaded();

    llvmbe::RegisterEmitCategoryALU();
    llvmbe::RegisterEmitCategoryControl();
    llvmbe::RegisterEmitCategoryFPU();
    llvmbe::RegisterEmitCategoryMemory();

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
    llvm_shutdown();
  }
}


LLVMBackend::LLVMBackend() :
    Backend() {
  InitializeIfNeeded();
}

LLVMBackend::~LLVMBackend() {
}

CodeUnitBuilder* LLVMBackend::CreateCodeUnitBuilder() {
  return NULL;
}

LibraryLoader* LLVMBackend::CreateLibraryLoader() {
  return NULL;
}

JIT* LLVMBackend::CreateJIT(xe_memory_ref memory, FunctionTable* fn_table) {
  return new LLVMJIT(memory, fn_table);
}
