/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PRIVATE_H_
#define XENIA_CPU_PRIVATE_H_

#include <gflags/gflags.h>


DECLARE_bool(trace_instructions);
DECLARE_bool(trace_registers);
DECLARE_bool(trace_branches);
DECLARE_bool(trace_user_calls);
DECLARE_bool(trace_kernel_calls);
DECLARE_uint64(trace_thread_mask);

DECLARE_string(load_module_map);

DECLARE_string(dump_path);
DECLARE_bool(dump_module_map);


#endif  // XENIA_CPU_PRIVATE_H_
