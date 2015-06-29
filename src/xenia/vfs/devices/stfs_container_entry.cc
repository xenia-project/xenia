/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/stfs_container_entry.h"

#include "xenia/base/math.h"
#include "xenia/vfs/devices/stfs_container_file.h"

namespace xe {
namespace vfs {

StfsContainerEntry::StfsContainerEntry(Device* device, Entry* parent,
                                       std::string path, MappedMemory* mmap)
    : Entry(device, parent, path),
      mmap_(mmap),
      data_offset_(0),
      data_size_(0) {}

StfsContainerEntry::~StfsContainerEntry() = default;

X_STATUS StfsContainerEntry::Open(KernelState* kernel_state,
                                  uint32_t desired_access,
                                  object_ref<XFile>* out_file) {
  *out_file = object_ref<XFile>(
      new StfsContainerFile(kernel_state, desired_access, this));
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
