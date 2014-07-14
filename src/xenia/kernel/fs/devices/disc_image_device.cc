/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/disc_image_device.h>

#include <xenia/kernel/fs/gdfx.h>
#include <xenia/kernel/fs/devices/disc_image_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;



DiscImageDevice::DiscImageDevice(const char* path, const xechar_t* local_path) :
    Device(path) {
  local_path_ = xestrdup(local_path);
  mmap_ = NULL;
  gdfx_ = NULL;
}

DiscImageDevice::~DiscImageDevice() {
  delete gdfx_;
  xe_mmap_release(mmap_);
  xe_free(local_path_);
}

int DiscImageDevice::Init() {
  mmap_ = xe_mmap_open(kXEFileModeRead, local_path_, 0, 0);
  if (!mmap_) {
    XELOGE("Disc image could not be mapped");
    return 1;
  }

  gdfx_ = new GDFX(mmap_);
  GDFX::Error error = gdfx_->Load();
  if (error != GDFX::kSuccess) {
    XELOGE("GDFX init failed: %d", error);
    return 1;
  }

  //gdfx_->Dump();

  return 0;
}

Entry* DiscImageDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("DiscImageDevice::ResolvePath(%s)", path);

  GDFXEntry* gdfx_entry = gdfx_->root_entry();

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
    gdfx_entry = gdfx_entry->GetChild(remaining);
    if (!gdfx_entry) {
      // Not found.
      return NULL;
    }

    // Shift the buffer down, unless we are at the end.
    if (!next_slash) {
      break;
    }
    XEIGNORE(xestrcpya(remaining, XECOUNT(remaining), next_slash + 1));
  }

  Entry::Type type = gdfx_entry->attributes & X_FILE_ATTRIBUTE_DIRECTORY ?
      Entry::kTypeDirectory : Entry::kTypeFile;
  return new DiscImageEntry(
      type, this, path, mmap_, gdfx_entry);
}

X_STATUS DiscImageDevice::QueryVolume(XVolumeInfo* out_info, size_t length) {
  assert_always();
  return X_STATUS_NOT_IMPLEMENTED;
}

X_STATUS DiscImageDevice::QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length) {
  assert_always();
  return X_STATUS_NOT_IMPLEMENTED;
}
