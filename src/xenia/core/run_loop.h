/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_RUN_LOOP_H_
#define XENIA_CORE_RUN_LOOP_H_

#include <xenia/common.h>
#include <xenia/core/ref.h>


struct xe_run_loop;
typedef struct xe_run_loop* xe_run_loop_ref;


xe_run_loop_ref xe_run_loop_create();
xe_run_loop_ref xe_run_loop_retain(xe_run_loop_ref run_loop);
void xe_run_loop_release(xe_run_loop_ref run_loop);

int xe_run_loop_pump(xe_run_loop_ref run_loop);
void xe_run_loop_quit(xe_run_loop_ref run_loop);

typedef void (*xe_run_loop_callback)(void* data);
void xe_run_loop_call(xe_run_loop_ref run_loop,
                      xe_run_loop_callback callback, void* data);


#endif  // XENIA_CORE_RUN_LOOP_H_
