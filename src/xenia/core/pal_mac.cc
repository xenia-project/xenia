/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/pal.h>

#include <CoreFoundation/CoreFoundation.h>
#include <asl.h>
#include <mach/mach_time.h>
#include <mach/kern_return.h>
#include <sys/types.h>
#include <sys/sysctl.h>


namespace {
typedef struct xe_pal_mac {
  double time_to_sec;
} xe_pal_mac_t;

xe_pal_mac_t* pal;
}


void xe_pal_dealloc();
int xe_pal_init(xe_pal_options_t options) {
  pal = (xe_pal_mac_t)xe_calloc(sizeof(pal));

  mach_timebase_info_data_t info;
  mach_timebase_info(&info);
  pal->time_to_sec = (double)((info.numer / info.denom) / 1000000000.0);

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

  // http://geekinfo.googlecode.com/svn-history/r74/trunk/src/macosxsystem.cpp
  int count;
  size_t size = sizeof(int);
  if (!sysctlbyname("hw.physicalcpu", &count, &size, NULL, 0)) {
    out_info->processors.physical_count = count;
  } else if (!sysctlbyname("hw.packages", &count, &size, NULL, 0)) {
    out_info->processors.physical_count = count;
  } else {
    out_info->processors.physical_count = 1;
  }
  if (!sysctlbyname("hw.logicalcpu", &count, &size, NULL, 0)) {
    out_info->processors.logical_count = count;
  } else if (!sysctlbyname("hw.ncpu", &count, &size, NULL, 0)) {
    out_info->processors.logical_count = count;
  } else {
    out_info->processors.logical_count = 1;
  }

  return 0;
}

double xe_pal_now() {
  // According to a bunch of random posts, CACurrentMediaTime is a better call:
  // https://devforums.apple.com/message/118806#118806
  // CACurrentMediaTime is based on mach_absolute_time(), which doesn't have a
  // dependency on QuartzCore:
  // http://developer.apple.com/library/mac/#qa/qa2004/qa1398.html

  // return (double)CFAbsoluteTimeGetCurrent();
  // return (double)CACurrentMediaTime();

  return (double)(mach_absolute_time() * pal->time_to_sec);
}
