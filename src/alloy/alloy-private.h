/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_ALLOY_PRIVATE_H_
#define ALLOY_ALLOY_PRIVATE_H_

#include <alloy/core.h>

#include <gflags/gflags.h>

DECLARE_bool(debug);
DECLARE_bool(always_disasm);

DECLARE_bool(validate_hir);

DECLARE_uint64(break_on_instruction);
DECLARE_uint64(break_on_memory);

#endif  // ALLOY_ALLOY_PRIVATE_H_
