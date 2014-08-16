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

MemoryMapping::MemoryMapping(uint8_t* address, size_t length)
    : address_(address), length_(length) {}

MemoryMapping::~MemoryMapping() {}

Entry::Entry(Type type, Device* device, const std::string& path)
    : type_(type), device_(device), path_(path) {
  assert_not_null(device);
  absolute_path_ = device->path() + path;
  // TODO(benvanik): last index of \, unless \ at end, then before that
  name_ = "";
}

Entry::~Entry() = default;
