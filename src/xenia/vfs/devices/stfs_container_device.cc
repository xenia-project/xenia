/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/stfs_container_device.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/stfs.h"
#include "xenia/vfs/devices/stfs_container_entry.h"
#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace vfs {

STFSContainerDevice::STFSContainerDevice(const std::string& mount_path,
                                         const std::wstring& local_path)
    : Device(mount_path), local_path_(local_path), stfs_(nullptr) {}

STFSContainerDevice::~STFSContainerDevice() { delete stfs_; }

bool STFSContainerDevice::Initialize() {
  mmap_ = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("STFS container could not be mapped");
    return false;
  }

  stfs_ = new STFS(mmap_.get());
  STFS::Error error = stfs_->Load();
  if (error != STFS::kSuccess) {
    XELOGE("STFS init failed: %d", error);
    return false;
  }

  // stfs_->Dump();

  return true;
}

std::unique_ptr<Entry> STFSContainerDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("STFSContainerDevice::ResolvePath(%s)", path);

  STFSEntry* stfs_entry = stfs_->root_entry();

  // Walk the path, one separator at a time.
  auto path_parts = xe::split_path(path);
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

}  // namespace vfs
}  // namespace xe
