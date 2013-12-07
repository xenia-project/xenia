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


GraphicsDriver::GraphicsDriver(Memory* memory) :
    memory_(memory),  address_translation_(0) {
  memset(&register_file_, 0, sizeof(register_file_));
}

GraphicsDriver::~GraphicsDriver() {
}
