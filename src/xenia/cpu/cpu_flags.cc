/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/cpu_flags.h"

DEFINE_string(cpu, "any", "CPU backend [any, x64].");

DEFINE_string(
    load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.");

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
DEFINE_int32(break_condition_gpr, -1, "GPR compared to");
DEFINE_uint64(break_condition_value, 0, "value compared against");
DEFINE_bool(break_condition_truncate, true, "truncate value to 32-bits");

DEFINE_bool(break_on_debugbreak, true, "int3 on JITed __debugbreak requests.");
