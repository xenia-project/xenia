/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/libjit/libjit_library_loader.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::libjit;


LibjitLibraryLoader::LibjitLibraryLoader(
    xe_memory_ref memory, kernel::ExportResolver* export_resolver) :
    LibraryLoader(memory, export_resolver) {
}

LibjitLibraryLoader::~LibjitLibraryLoader() {
}
