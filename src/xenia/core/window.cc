/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/window.h>


using namespace xe;
using namespace xe::core;


Window::Window(xe_run_loop_ref run_loop) {
  run_loop_ = xe_run_loop_retain(run_loop);
}

Window::~Window() {
  xe_run_loop_release(run_loop_);
}
