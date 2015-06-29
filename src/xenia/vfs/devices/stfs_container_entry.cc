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

STFSContainerEntry::STFSContainerEntry(Device* device, Entry* parent,
                                       std::string path, MappedMemory* mmap)
    : Entry(device, parent, path),
      mmap_(mmap),
      data_offset_(0),
      data_size_(0) {}

STFSContainerEntry::~STFSContainerEntry() = default;

X_STATUS STFSContainerEntry::Open(KernelState* kernel_state, Mode mode,
                                  bool async, XFile** out_file) {
  *out_file = new STFSContainerFile(kernel_state, mode, this);
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
