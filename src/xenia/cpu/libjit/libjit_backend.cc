/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/libjit/libjit_backend.h>

#include <xenia/cpu/libjit/libjit_emit.h>
#include <xenia/cpu/libjit/libjit_jit.h>
#include <xenia/cpu/sdb/symbol_table.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::libjit;
using namespace xe::cpu::sdb;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    libjit::LibjitRegisterEmitCategoryALU();
    libjit::LibjitRegisterEmitCategoryControl();
    libjit::LibjitRegisterEmitCategoryFPU();
    libjit::LibjitRegisterEmitCategoryMemory();

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


LibjitBackend::LibjitBackend() :
    Backend() {
  InitializeIfNeeded();
}

LibjitBackend::~LibjitBackend() {
}

LibraryLoader* LibjitBackend::CreateLibraryLoader() {
  return NULL;
}

JIT* LibjitBackend::CreateJIT(xe_memory_ref memory, SymbolTable* sym_table) {
  return new LibjitJIT(memory, sym_table);
}
