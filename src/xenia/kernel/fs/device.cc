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

Device::Device(const std::string& path) : path_(path) {}

Device::~Device() {}

const char* Device::path() const { return path_.c_str(); }
