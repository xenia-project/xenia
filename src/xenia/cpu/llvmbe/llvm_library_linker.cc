/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvmbe/llvm_library_linker.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::llvmbe;


LLVMLibraryLinker::LLVMLibraryLinker(
    xe_memory_ref memory, kernel::ExportResolver* export_resolver) :
    LibraryLinker(memory, export_resolver) {
}

LLVMLibraryLinker::~LLVMLibraryLinker() {
}
