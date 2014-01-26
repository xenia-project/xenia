/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/cpu-private.h>


// Tracing:
DEFINE_bool(trace_instructions, false,
    "Trace all instructions.");
DEFINE_bool(trace_registers, false,
    "Trace register values when tracing instructions.");
DEFINE_bool(trace_branches, false,
    "Trace all branches.");
DEFINE_bool(trace_user_calls, false,
    "Trace all user function calls.");
DEFINE_bool(trace_kernel_calls, false,
    "Trace all kernel function calls.");
DEFINE_uint64(trace_thread_mask, -1,
    "Trace threads with IDs in the mask, or -1 for all.");


// Debugging:
DEFINE_string(load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.");


// Dumping:
DEFINE_string(dump_path, "build/",
    "Directory that dump files are placed into.");
DEFINE_bool(dump_module_bitcode, true,
    "Writes the module bitcode both before and after optimizations.");
DEFINE_bool(dump_module_map, true,
    "Dumps the module symbol database.");
