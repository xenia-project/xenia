/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_PAL_H_
#define XENIA_CORE_PAL_H_

#include <xenia/common.h>
#include <xenia/core/ref.h>


typedef struct {
  int reserved;
} xe_pal_options_t;


struct xe_pal;
typedef struct xe_pal* xe_pal_ref;


xe_pal_ref xe_pal_create(xe_pal_options_t options);
xe_pal_ref xe_pal_retain(xe_pal_ref pal);
void xe_pal_release(xe_pal_ref pal);


#endif  // XENIA_CORE_PAL_H_
