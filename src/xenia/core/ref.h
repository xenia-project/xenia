/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_REF_H_
#define XENIA_CORE_REF_H_

#include <xenia/common.h>


typedef struct {
  volatile int32_t count;
} xe_ref_t;
typedef xe_ref_t* xe_ref;

typedef void (*xe_ref_dealloc_t)(xe_ref);


void xe_ref_init(xe_ref ref);
void xe_ref_retain(xe_ref ref);
void xe_ref_release(xe_ref ref, xe_ref_dealloc_t dealloc);


#endif  // XENIA_CORE_REF_H_
