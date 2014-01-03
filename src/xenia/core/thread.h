/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_THREAD_H_
#define XENIA_CORE_THREAD_H_

#include <xenia/common.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>


struct xe_thread;
typedef struct xe_thread* xe_thread_ref;


typedef void (*xe_thread_callback)(void* param);


xe_thread_ref xe_thread_create(
    const char* name, xe_thread_callback callback, void* param);
xe_thread_ref xe_thread_retain(xe_thread_ref thread);
void xe_thread_release(xe_thread_ref thread);

int xe_thread_start(xe_thread_ref thread);
void xe_thread_join(xe_thread_ref thread);


#endif  // XENIA_CORE_THREAD_H_
