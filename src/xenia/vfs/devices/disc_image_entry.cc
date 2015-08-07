/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_entry.h"

#include <algorithm>

#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_image_file.h"

namespace xe {
namespace vfs {

DiscImageEntry::DiscImageEntry(Device* device, Entry* parent, std::string path,
                               MappedMemory* mmap)
    : Entry(device, parent, path),
      mmap_(mmap),
      data_offset_(0),
      data_size_(0) {}

DiscImageEntry::~DiscImageEntry() = default;

X_STATUS DiscImageEntry::Open(kernel::KernelState* kernel_state,
                              uint32_t desired_access,
                              kernel::object_ref<kernel::XFile>* out_file) {
  *out_file = kernel::object_ref<kernel::XFile>(
      new DiscImageFile(kernel_state, desired_access, this));
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> DiscImageEntry::OpenMapped(
    MappedMemory::Mode mode, size_t offset, size_t length) {
  if (mode != MappedMemory::Mode::kRead) {
    // Only allow reads.
    return nullptr;
  }

  size_t real_offset = data_offset_ + offset;
  size_t real_length = length ? std::min(length, data_size_) : data_size_;
  return mmap_->Slice(mode, real_offset, real_length);
}

}  // namespace vfs
}  // namespace xe
