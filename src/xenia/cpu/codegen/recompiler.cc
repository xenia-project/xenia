/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/codegen/recompiler.h>

#include <llvm/Linker.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/system_error.h>
#include <llvm/Support/Threading.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/llvm_exports.h>
#include <xenia/cpu/sdb.h>
#include <xenia/cpu/codegen/module_generator.h>
#include <xenia/cpu/ppc/instr.h>
#include <xenia/cpu/ppc/state.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::codegen;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


Recompiler::Recompiler(
    xe_memory_ref memory, shared_ptr<ExportResolver> export_resolver,
    shared_ptr<SymbolDatabase> sdb, const char* module_name) {
  memory_ = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
  sdb_ = sdb;
}

Recompiler::~Recompiler() {
  xe_memory_release(memory_);
}

int Recompiler::Process() {
  // Check to see if a cached result exists on disk - if so, use it.
  if (!LoadLibrary(library_path)) {
    // Succeeded - done!
    return 0;
  }

  // Generate all the code and dump it to code units.
  // This happens in multiple threads.
  if (GenerateCodeUnits()) {
    XELOGCPU("Failed to generate code units for module");
    return 1;
  }

  // Link all of the generated code units. This runs any link-time optimizations
  // and other per-library operations.
  if (LinkCodeUnits()) {
    XELOGCPU("Failed to link code units");
    return 1;
  }

  // Load the built library now.
  if (LoadLibrary(library_path)) {
    XELOGCPU("Failed to load the generated library");
    return 1;
  }

  return 0;
}

int Recompiler::GenerateCodeUnits() {
  xe_system_info sys_info;
  XEEXPECTZERO(xe_pal_get_system_info(&sys_info));
  // sys_info.processors.physical_count;
  // sys_info.processors.logical_count;

  // Queue up all functions to process.

  // Spawn worker threads to process the queue.

  // Wait until all threads complete.

  return 0;

XECLEANUP:
  return 1;
}

int Recompiler::LinkCodeUnits() {
  // Invoke linker.

  return 0;
}

int Recompiler::LoadLibrary(const char* path) {
  // Check file exists.

  // TODO(benvanik): version check somehow?

  // Load library.

  return 0;
}
