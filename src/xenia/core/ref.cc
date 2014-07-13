/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/ref.h>


void xe_ref_init(xe_ref_t* ref) {
  ref->count = 1;
}

void xe_ref_retain(xe_ref_t* ref) {
  poly::atomic_inc(&ref->count);
}

void xe_ref_release(xe_ref_t* ref, xe_ref_dealloc_t dealloc) {
  if (!ref) {
    return;
  }
  if (!poly::atomic_dec(&ref->count)) {
    if (dealloc) {
      dealloc(ref);
    }
    xe_free(ref);
  }
}
