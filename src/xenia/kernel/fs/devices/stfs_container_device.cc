/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/devices/stfs_container_device.h"

#include "poly/math.h"
#include "xenia/kernel/fs/stfs.h"
#include "xenia/kernel/fs/devices/stfs_container_entry.h"
#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace kernel {
namespace fs {

STFSContainerDevice::STFSContainerDevice(const std::string& path,
                                         const std::wstring& local_path)
    : Device(path), local_path_(local_path), stfs_(nullptr) {}

STFSContainerDevice::~STFSContainerDevice() { delete stfs_; }

int STFSContainerDevice::Init() {
  mmap_ =
      poly::MappedMemory::Open(local_path_, poly::MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("STFS container could not be mapped");
    return 1;
  }

  stfs_ = new STFS(mmap_.get());
  STFS::Error error = stfs_->Load();
  if (error != STFS::kSuccess) {
    XELOGE("STFS init failed: %d", error);
    return 1;
  }

  //stfs_->Dump();

  return 0;
}

std::unique_ptr<Entry> STFSContainerDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("STFSContainerDevice::ResolvePath(%s)", path);

  STFSEntry* stfs_entry = stfs_->root_entry();

  // Walk the path, one separator at a time.
  auto path_parts = poly::split_path(path);
  for (auto& part : path_parts) {
    stfs_entry = stfs_entry->GetChild(part.c_str());
    if (!stfs_entry) {
      // Not found.
      return nullptr;
    }
  }

  return std::make_unique<STFSContainerEntry>(this, path, mmap_.get(),
                                              stfs_entry);
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe
