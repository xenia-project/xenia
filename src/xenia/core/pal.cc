/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/pal.h>


typedef struct xe_pal {
  xe_ref_t ref;

} xe_pal_t;


xe_pal_ref xe_pal_create(xe_pal_options_t options) {
  xe_pal_ref pal = (xe_pal_ref)xe_calloc(sizeof(xe_pal_t));
  xe_ref_init((xe_ref)pal);

  //

  return pal;
}

void xe_pal_dealloc(xe_pal_ref pal) {
  //
}

xe_pal_ref xe_pal_retain(xe_pal_ref pal) {
  xe_ref_retain((xe_ref)pal);
  return pal;
}

void xe_pal_release(xe_pal_ref pal) {
  xe_ref_release((xe_ref)pal, (xe_ref_dealloc_t)xe_pal_dealloc);
}
