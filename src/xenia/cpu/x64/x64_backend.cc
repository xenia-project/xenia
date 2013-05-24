/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_backend.h>

#include <xenia/cpu/sdb/symbol_table.h>
#include <xenia/cpu/x64/x64_emit.h>
#include <xenia/cpu/x64/x64_jit.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::cpu::x64;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    X64RegisterEmitCategoryALU();
    X64RegisterEmitCategoryControl();
    X64RegisterEmitCategoryFPU();
    X64RegisterEmitCategoryMemory();

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


X64Backend::X64Backend() :
    Backend() {
  InitializeIfNeeded();
}

X64Backend::~X64Backend() {
}

JIT* X64Backend::CreateJIT(xe_memory_ref memory, SymbolTable* sym_table) {
  return new X64JIT(memory, sym_table);
}
