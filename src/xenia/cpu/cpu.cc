/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/cpu-private.h"

// Debugging:
DEFINE_string(
    load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.");

// Dumping:
DEFINE_string(dump_path, "build/",
              "Directory that dump files are placed into.");
DEFINE_bool(dump_module_bitcode, true,
            "Writes the module bitcode both before and after optimizations.");
DEFINE_bool(dump_module_map, true, "Dumps the module symbol database.");

#if 0 && DEBUG
#define DEFAULT_DEBUG_FLAG true
#else
#define DEFAULT_DEBUG_FLAG false
#endif

DEFINE_bool(debug, DEFAULT_DEBUG_FLAG,
            "Allow debugging and retain debug information.");
DEFINE_bool(
    always_disasm, false,
    "Always add debug info to functions, even when no debugger is attached.");

DEFINE_bool(validate_hir, false,
            "Perform validation checks on the HIR during compilation.");

// Breakpoints:
DEFINE_uint64(break_on_instruction, 0,
              "int3 before the given guest address is executed.");
DEFINE_uint64(break_on_memory, 0,
              "int3 on read/write to the given memory address.");
DEFINE_bool(break_on_debugbreak, true, "int3 on JITed __debugbreak requests.");
