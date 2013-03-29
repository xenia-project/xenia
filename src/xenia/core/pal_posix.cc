/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/pal.h>


namespace {
typedef struct xe_pal_posix {

} xe_pal_posix_t;

xe_pal_posix_t* pal;
}


void xe_pal_dealloc();
int xe_pal_init(xe_pal_options_t options) {
  pal = (xe_pal_posix_t)xe_calloc(sizeof(pal));

  //

  atexit(xe_pal_dealloc);
  return 0;
}

void xe_pal_dealloc() {
  //

  xe_free(pal);
}

int xe_pal_get_system_info(xe_system_info* out_info) {
  xe_zero_struct(out_info, sizeof(xe_system_info));
  out_info->processors.physical_count = 1;
  out_info->processors.logical_count  = 1;

#if defined(_SC_NPROCESSORS_ONLN)
  int nproc = sysconf(_SC_NPROCESSORS_ONLN);
  if (nproc >= 1) {
    // Only able to get logical count.
    sysInfo->processors.logicalCount = nproc;
  }
#else
#warning no calls to get processor counts
#endif  // _SC_NPROCESSORS_ONLN

  return 0;
}

double xe_pal_now() {
  // http://www.kernel.org/doc/man-pages/online/pages/man2/clock_gettime.2.html
  // http://juliusdavies.ca/posix_clocks/clock_realtime_linux_faq.html
  struct timespec ts;
  CPIGNORE(clock_gettime(CLOCK_MONOTONIC, &ts));
  return (double)(ts.tv_sec + (ts.tv_nsec * 1000000000.0));
}
