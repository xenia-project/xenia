/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


int xenia_info(int argc, xechar_t **argv) {
  int result_code = 1;

  xe_pal_ref pal = NULL;
  xe_memory_ref memory = NULL;
  shared_ptr<Processor> processor;
  shared_ptr<Runtime> runtime;

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

  processor = shared_ptr<Processor>(new Processor(pal, memory));
  XEEXPECTZERO(processor->Setup());

  runtime = shared_ptr<Runtime>(new Runtime(pal, processor, XT("")));

  XEEXPECTZERO(runtime->LoadModule(path));

  result_code = 0;
XECLEANUP:
  xe_memory_release(memory);
  xe_pal_release(pal);
  return result_code;
}
XE_MAIN_THUNK(xenia_info);
