/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

/**
 * This file is shared between xethunk and the loader to pass structures
 * between the two. Since this file is compiled with the LLVM clang it cannot
 * include any other files.
 */

#ifndef XENIA_CPU_XELIB_H_
#define XENIA_CPU_XELIB_H_


typedef struct {
  void*   memory_base;

  // TODO(benvanik): execute call thunk
} xe_module_init_options_t;


#endif  // XENIA_CPU_XELIB_H_
