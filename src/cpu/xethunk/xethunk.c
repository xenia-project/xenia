/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

/**
 * This file is compiled with clang to produce LLVM bitcode.
 * When the emulator goes to build a full module it then imports this code into
 * the generated module to provide globals/other shared values.
 *
 * Changes to this file require building a new version and checking it into the
 * repo on a machine that has clang.
 *
 *     # rebuild the xethunk.bc/.ll files:
 *     xb xethunk
 */

// NOTE: only headers in this directory should be included.
#include "xethunk.h"


// The base pointer that all guest addresses should be relative to.
static void* xe_memory_base;


int xe_module_init(xe_module_init_options_t *options) {
  xe_memory_base = options->memory_base;

  // TODO(benvanik): setup call table, etc?

  return 0;
}

void xe_module_uninit() {
}
