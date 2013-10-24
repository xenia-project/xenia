/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/fs/entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl::fs;


MemoryMapping::MemoryMapping(uint8_t* address, size_t length) :
    address_(address), length_(length) {
}

MemoryMapping::~MemoryMapping() {
}


Entry::Entry(Type type, Device* device, const char* path) :
    type_(type),
    device_(device) {
  path_ = xestrdupa(path);
  // TODO(benvanik): last index of \, unless \ at end, then before that
  name_ = NULL;
}

Entry::~Entry() {
  xe_free(path_);
  xe_free(name_);
}
