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


int xe_pal_init(xe_pal_options_t options);


typedef struct {
  struct {
    uint32_t  physical_count;
    uint32_t  logical_count;
  } processors;
} xe_system_info;

int xe_pal_get_system_info(xe_system_info* out_info);

double xe_pal_now();


#endif  // XENIA_CORE_PAL_H_
