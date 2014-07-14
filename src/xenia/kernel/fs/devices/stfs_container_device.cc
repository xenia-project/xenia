/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/stfs_container_device.h>

#include <xenia/kernel/fs/stfs.h>
#include <xenia/kernel/fs/devices/stfs_container_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;



STFSContainerDevice::STFSContainerDevice(
    const char* path, const xechar_t* local_path) :
    Device(path) {
  local_path_ = xestrdup(local_path);
  mmap_ = NULL;
  stfs_ = NULL;
}

STFSContainerDevice::~STFSContainerDevice() {
  delete stfs_;
  xe_mmap_release(mmap_);
  xe_free(local_path_);
}

int STFSContainerDevice::Init() {
  mmap_ = xe_mmap_open(kXEFileModeRead, local_path_, 0, 0);
  if (!mmap_) {
    XELOGE("STFS container could not be mapped");
    return 1;
  }

  stfs_ = new STFS(mmap_);
  STFS::Error error = stfs_->Load();
  if (error != STFS::kSuccess) {
    XELOGE("STFS init failed: %d", error);
    return 1;
  }

  stfs_->Dump();

  return 0;
}

Entry* STFSContainerDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("STFSContainerDevice::ResolvePath(%s)", path);

  STFSEntry* stfs_entry = stfs_->root_entry();

  // Walk the path, one separator at a time.
  // We copy it into the buffer and shift it left over and over.
  char remaining[poly::max_path];
  XEIGNORE(xestrcpya(remaining, XECOUNT(remaining), path));
  while (remaining[0]) {
    char* next_slash = xestrchra(remaining, '\\');
    if (next_slash == remaining) {
      // Leading slash - shift
      XEIGNORE(xestrcpya(remaining, XECOUNT(remaining), remaining + 1));
      continue;
    }

    // Make the buffer just the name.
    if (next_slash) {
      *next_slash = 0;
    }

    // Look up in the entry.
    stfs_entry = stfs_entry->GetChild(remaining);
    if (!stfs_entry) {
      // Not found.
      return NULL;
    }

    // Shift the buffer down, unless we are at the end.
    if (!next_slash) {
      break;
    }
    XEIGNORE(xestrcpya(remaining, XECOUNT(remaining), next_slash + 1));
  }

  Entry::Type type = stfs_entry->attributes & X_FILE_ATTRIBUTE_DIRECTORY ?
      Entry::kTypeDirectory : Entry::kTypeFile;
  return new STFSContainerEntry(
      type, this, path, mmap_, stfs_entry);
}


X_STATUS STFSContainerDevice::QueryVolume(XVolumeInfo* out_info, size_t length) {
  assert_always();
  return X_STATUS_NOT_IMPLEMENTED;
}

X_STATUS STFSContainerDevice::QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length) {
  assert_always();
  return X_STATUS_NOT_IMPLEMENTED;
}
