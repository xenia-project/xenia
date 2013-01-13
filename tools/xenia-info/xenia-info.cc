/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>


int xenia_info(int argc, xechar_t **argv) {
  int result_code = 1;

  xe_pal_ref pal = NULL;
  xe_memory_ref memory = NULL;
  xe_cpu_ref cpu = NULL;
  xe_kernel_ref kernel = NULL;
  xe_module_ref module = NULL;

  // TODO(benvanik): real command line parsing.
  if (argc < 2) {
    printf("usage: xenia-info some.xex\n");
    return 1;
  }
  const xechar_t *path = argv[1];

  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  pal = xe_pal_create(pal_options);
  XEEXPECTNOTNULL(pal);

  xe_memory_options_t memory_options;
  xe_zero_struct(&memory_options, sizeof(memory_options));
  memory = xe_memory_create(pal, memory_options);
  XEEXPECTNOTNULL(memory);

  xe_cpu_options_t cpu_options;
  xe_zero_struct(&cpu_options, sizeof(cpu_options));
  cpu = xe_cpu_create(pal, memory, cpu_options);
  XEEXPECTNOTNULL(cpu);

  xe_kernel_options_t kernel_options;
  xe_zero_struct(&kernel_options, sizeof(kernel_options));
  kernel = xe_kernel_create(pal, cpu, kernel_options);
  XEEXPECTNOTNULL(kernel);

  module = xe_kernel_load_module(kernel, path);
  XEEXPECTNOTNULL(module);

  xe_module_dump(module);

  result_code = 0;
XECLEANUP:
  xe_module_release(module);
  xe_kernel_release(kernel);
  xe_cpu_release(cpu);
  xe_memory_release(memory);
  xe_pal_release(pal);
  return result_code;
}
XE_MAIN_THUNK(xenia_info);
