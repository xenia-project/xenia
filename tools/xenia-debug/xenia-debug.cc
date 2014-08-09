/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/xdb.h>

#include <gflags/gflags.h>


//using namespace xdb;


int xenia_debug(int argc, xechar_t** argv) {
  int result_code = 1;

  // Create platform abstraction layer.
  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  XEEXPECTZERO(xe_pal_init(pal_options));


  result_code = 0;
XECLEANUP:
  if (result_code) {
    XEFATAL("Failed to launch debugger: %d", result_code);
  }
  return result_code;
}
XE_MAIN_WINDOW_THUNK(xenia_debug, XETEXT("xenia-debug"), "xenia-debug");
