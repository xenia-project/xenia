/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/fs/devices/disc_image_device.h"

#include "poly/math.h"
#include "xenia/kernel/fs/gdfx.h"
#include "xenia/kernel/fs/devices/disc_image_entry.h"

namespace xe {
namespace kernel {
namespace fs {

DiscImageDevice::DiscImageDevice(const std::string& path,
                                 const std::wstring& local_path)
    : Device(path), local_path_(local_path), gdfx_(nullptr) {}

DiscImageDevice::~DiscImageDevice() { delete gdfx_; }

int DiscImageDevice::Init() {
  mmap_ =
      poly::MappedMemory::Open(local_path_, poly::MappedMemory::Mode::kRead);
  if (!mmap_) {
    XELOGE("Disc image could not be mapped");
    return 1;
  }

  gdfx_ = new GDFX(mmap_.get());
  GDFX::Error error = gdfx_->Load();
  if (error != GDFX::kSuccess) {
    XELOGE("GDFX init failed: %d", error);
    return 1;
  }

  // gdfx_->Dump();

  return 0;
}

std::unique_ptr<Entry> DiscImageDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("DiscImageDevice::ResolvePath(%s)", path);

  GDFXEntry* gdfx_entry = gdfx_->root_entry();

  // Walk the path, one separator at a time.
  auto path_parts = poly::split_path(path);
  for (auto& part : path_parts) {
    gdfx_entry = gdfx_entry->GetChild(part.c_str());
    if (!gdfx_entry) {
      // Not found.
      return nullptr;
    }
  }

  return std::make_unique<DiscImageEntry>(this, path, mmap_.get(), gdfx_entry);
}

}  // namespace fs
}  // namespace kernel
}  // namespace xe
