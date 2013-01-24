/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/cpu-private.h"


// Dumping:
DEFINE_string(dump_path, "build/",
    "Directory that dump files are placed into.");
DEFINE_bool(dump_module_bitcode, true,
    "Writes the module bitcode both before and after optimizations.");
DEFINE_bool(dump_module_map, true,
    "Dumps the module symbol database.");


// Optimizations:
DEFINE_bool(optimize_ir_modules, true,
    "Whether to run LLVM optimizations on modules.");
DEFINE_bool(optimize_ir_functions, true,
    "Whether to run LLVM optimizations on functions.");
