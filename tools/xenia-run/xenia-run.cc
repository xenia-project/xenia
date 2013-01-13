/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>


typedef struct {
  xe_pal_ref      pal;
  xe_memory_ref   memory;
  xe_cpu_ref      cpu;
  xe_kernel_ref   kernel;
  xe_module_ref   module;
} xenia_run_t;


int setup_run(xenia_run_t *run, const xechar_t *path) {
    xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  run->pal = xe_pal_create(pal_options);
  XEEXPECTNOTNULL(run->pal);

  xe_memory_options_t memory_options;
  xe_zero_struct(&memory_options, sizeof(memory_options));
  run->memory = xe_memory_create(run->pal, memory_options);
  XEEXPECTNOTNULL(run->memory);

  xe_cpu_options_t cpu_options;
  xe_zero_struct(&cpu_options, sizeof(cpu_options));
  run->cpu = xe_cpu_create(run->pal, run->memory, cpu_options);
  XEEXPECTNOTNULL(run->cpu);

  xe_kernel_options_t kernel_options;
  xe_zero_struct(&kernel_options, sizeof(kernel_options));
  run->kernel = xe_kernel_create(run->pal, run->cpu, kernel_options);
  XEEXPECTNOTNULL(run->kernel);

  run->module = xe_kernel_load_module(run->kernel, path);
  XEEXPECTNOTNULL(run->module);

  return 0;

XECLEANUP:
  return 1;
}

void destroy_run(xenia_run_t *run) {
  xe_module_release(run->module);
  xe_kernel_release(run->kernel);
  xe_cpu_release(run->cpu);
  xe_memory_release(run->memory);
  xe_pal_release(run->pal);
  xe_free(run);
}

int xenia_run(int argc, xechar_t **argv) {
  // Dummy call to keep the GPU code linking in to ensure it's working.
  do_gpu_stuff();

  int result_code = 1;

  // TODO(benvanik): real command line parsing.
  if (argc < 2) {
    printf("usage: xenia-run some.xex\n");
    return 1;
  }
  const xechar_t *path = argv[1];

  xenia_run_t *run = (xenia_run_t*)xe_calloc(sizeof(xenia_run_t));
  XEEXPECTNOTNULL(run);

  result_code = setup_run(run, path);
  XEEXPECTZERO(result_code);

  xe_module_dump(run->module);

  xe_kernel_launch_module(run->kernel, run->module);

  // TODO(benvanik): wait until the module thread exits
  destroy_run(run);
  return 0;

XECLEANUP:
  if (run) {
    destroy_run(run);
  }
  return result_code;
}
XE_MAIN_THUNK(xenia_run);
