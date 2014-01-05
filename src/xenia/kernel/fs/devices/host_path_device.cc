/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/devices/host_path_device.h>

#include <xenia/kernel/fs/devices/host_path_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


HostPathDevice::HostPathDevice(const char* path, const xechar_t* local_path) :
    Device(path) {
  local_path_ = xestrdup(local_path);
}

HostPathDevice::~HostPathDevice() {
  xe_free(local_path_);
}

Entry* HostPathDevice::ResolvePath(const char* path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo

  XELOGFS("HostPathDevice::ResolvePath(%s)", path);

#if XE_WCHAR
  xechar_t rel_path[XE_MAX_PATH];
  XEIGNORE(xestrwiden(rel_path, XECOUNT(rel_path), path));
#else
  const xechar_t* rel_path = path;
#endif

  xechar_t full_path[XE_MAX_PATH];
  xe_path_join(local_path_, rel_path, full_path, XECOUNT(full_path));

  // Swap around path separators.
  if (XE_PATH_SEPARATOR != '\\') {
    for (size_t n = 0; n < XECOUNT(full_path); n++) {
      if (full_path[n] == 0) {
        break;
      }
      if (full_path[n] == '\\') {
        full_path[n] = XE_PATH_SEPARATOR;
      }
    }
  }

  // TODO(benvanik): get file info
  // TODO(benvanik): fail if does not exit
  // TODO(benvanik): switch based on type

  Entry::Type type = Entry::kTypeFile;
  HostPathEntry* entry = new HostPathEntry(type, this, path, full_path);
  return entry;
}
