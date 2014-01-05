/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/device.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


Device::Device(const char* path) {
  path_ = xestrdupa(path);
}

Device::~Device() {
  xe_free(path_);
}

const char* Device::path() {
  return path_;
}
