/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_driver.h>


using namespace xe;
using namespace xe::gpu;


GraphicsDriver::GraphicsDriver(xe_memory_ref memory) :
    address_translation_(0) {
  memory_ = xe_memory_retain(memory);

  memset(&register_file_, 0, sizeof(register_file_));
}

GraphicsDriver::~GraphicsDriver() {
  xe_memory_release(memory_);
}
