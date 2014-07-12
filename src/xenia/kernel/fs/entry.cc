/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/entry.h>
#include <xenia/kernel/fs/device.h>

using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


MemoryMapping::MemoryMapping(uint8_t* address, size_t length) :
    address_(address), length_(length) {
}

MemoryMapping::~MemoryMapping() {
}


Entry::Entry(Type type, Device* device, const char* path) :
    type_(type),
    device_(device) {
  assert_not_null(device);
  path_ = xestrdupa(path);
  // TODO(benvanik): *shudder*
  absolute_path_ = xestrdupa((std::string(device->path()) + std::string(path)).c_str());
  // TODO(benvanik): last index of \, unless \ at end, then before that
  name_ = NULL;
}

Entry::~Entry() {
  xe_free(name_);
  xe_free(path_);
  xe_free(absolute_path_);
}
