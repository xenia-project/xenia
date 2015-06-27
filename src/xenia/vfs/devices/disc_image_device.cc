/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_device.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/gdfx.h"
#include "xenia/vfs/devices/disc_image_entry.h"

namespace xe {
namespace vfs {

DiscImageDevice::DiscImageDevice(const std::string& mount_path,
                                 const std::wstring& local_path)
    : Device(mount_path), local_path_(local_path), gdfx_(nullptr) {}

DiscImageDevice::~DiscImageDevice() { delete gdfx_; }

bool DiscImageDevice::Initialize() {
  mmap_ = MappedMemory::Open(local_path_, MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("Disc image could not be mapped");
    return false;
  }

  gdfx_ = new GDFX(mmap_.get());
  GDFX::Error error = gdfx_->Load();
  if (error != GDFX::kSuccess) {
    XELOGE("GDFX init failed: %d", error);
    return false;
  }

  // gdfx_->Dump();

  return true;
}

std::unique_ptr<Entry> DiscImageDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("DiscImageDevice::ResolvePath(%s)", path);

  GDFXEntry* gdfx_entry = gdfx_->root_entry();

  // Walk the path, one separator at a time.
  auto path_parts = xe::split_path(path);
  for (auto& part : path_parts) {
    gdfx_entry = gdfx_entry->GetChild(part.c_str());
    if (!gdfx_entry) {
      // Not found.
      return nullptr;
    }
  }

  return std::make_unique<DiscImageEntry>(this, path, mmap_.get(), gdfx_entry);
}

}  // namespace vfs
}  // namespace xe
