/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/device.h>

namespace xe {
namespace kernel {
namespace fs {

Device::Device(const std::string& path) : path_(path) {}

Device::~Device() = default;

}  // namespace fs
}  // namespace kernel
}  // namespace xe
