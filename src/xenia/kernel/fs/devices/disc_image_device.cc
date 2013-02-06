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


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


namespace {


class DiscImageMemoryMapping : public MemoryMapping {
public:
  DiscImageMemoryMapping(uint8_t* address, size_t length, xe_mmap_ref mmap) :
      MemoryMapping(address, length) {
    mmap_ = xe_mmap_retain(mmap);
  }

  virtual ~DiscImageMemoryMapping() {
    xe_mmap_release(mmap_);
  }

private:
  xe_mmap_ref mmap_;
};


class DiscImageFileEntry : public FileEntry {
public:
  DiscImageFileEntry(Device* device, const char* path,
                     xe_mmap_ref mmap, GDFXEntry* gdfx_entry) :
      FileEntry(device, path),
      gdfx_entry_(gdfx_entry) {
    mmap_ = xe_mmap_retain(mmap);
  }

  virtual ~DiscImageFileEntry() {
    xe_mmap_release(mmap_);
  }

  virtual MemoryMapping* CreateMemoryMapping(
      xe_file_mode file_mode, const size_t offset, const size_t length) {
    if (file_mode & kXEFileModeWrite) {
      // Only allow reads.
      return NULL;
    }

    size_t real_offset = gdfx_entry_->offset + offset;
    size_t real_length = length ?
        MIN(length, gdfx_entry_->size) : gdfx_entry_->size;
    return new DiscImageMemoryMapping(
        xe_mmap_get_addr(mmap_) + real_offset,
        real_length,
        mmap_);
  }

private:
  xe_mmap_ref mmap_;
  GDFXEntry*  gdfx_entry_;
};


}


DiscImageDevice::DiscImageDevice(xe_pal_ref pal, const char* path,
                                 const xechar_t* local_path) :
    Device(pal, path) {
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
  mmap_ = xe_mmap_open(pal_, kXEFileModeRead, local_path_, 0, 0);
  if (!mmap_) {
    XELOGE(XT("Disc image could not be mapped"));
    return 1;
  }

  gdfx_ = new GDFX(mmap_);
  GDFX::Error error = gdfx_->Load();
  if (error != GDFX::kSuccess) {
    XELOGE(XT("GDFX init failed: %d"), error);
    return 1;
  }

  //gdfx_->Dump();

  return 0;
}

Entry* DiscImageDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS(XT("DiscImageDevice::ResolvePath(%s)"), path);

  GDFXEntry* gdfx_entry = gdfx_->root_entry();

  // Walk the path, one separator at a time.
  // We copy it into the buffer and shift it left over and over.
  char remaining[XE_MAX_PATH];
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

  if (gdfx_entry->attributes & GDFXEntry::kAttrFolder) {
    //return new DiscImageDirectoryEntry(mmap_, gdfx_entry);
    XEASSERTALWAYS();
    return NULL;
  } else {
    return new DiscImageFileEntry(this, path, mmap_, gdfx_entry);
  }
}
