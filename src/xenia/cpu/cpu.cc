/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/cpu-private.h"

DEFINE_string(cpu, "any", "CPU backend [any, x64].");

DEFINE_string(
    load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.");

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
DEFINE_bool(disassemble_functions, false,
            "Disassemble functions during generation.");

DEFINE_bool(trace_functions, false,
            "Generate tracing for function statistics.");
DEFINE_bool(trace_function_coverage, false,
            "Generate tracing for function instruction coverage statistics.");
DEFINE_bool(trace_function_references, false,
            "Generate tracing for function address references.");
DEFINE_bool(trace_function_data, false,
            "Generate tracing for function result data.");

DEFINE_bool(validate_hir, false,
            "Perform validation checks on the HIR during compilation.");

// Breakpoints:
DEFINE_uint64(break_on_instruction, 0,
              "int3 before the given guest address is executed.");
DEFINE_bool(break_on_debugbreak, true, "int3 on JITed __debugbreak requests.");
