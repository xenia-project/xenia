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

#include <xenia/kernel/objects/xfile.h>

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
  xechar_t rel_path[poly::max_path];
  XEIGNORE(xestrwiden(rel_path, XECOUNT(rel_path), path));
#else
  const xechar_t* rel_path = path;
#endif

  xechar_t full_path[poly::max_path];
  xe_path_join(local_path_, rel_path, full_path, XECOUNT(full_path));

  // Swap around path separators.
  if (poly::path_separator != '\\') {
    for (size_t n = 0; n < XECOUNT(full_path); n++) {
      if (full_path[n] == 0) {
        break;
      }
      if (full_path[n] == '\\') {
        full_path[n] = poly::path_separator;
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

// TODO(gibbed): call into HostPathDevice?
X_STATUS HostPathDevice::QueryVolume(XVolumeInfo* out_info, size_t length) {
  assert_not_null(out_info);
  const char* name = "test"; // TODO(gibbed): actual value

  auto end = (uint8_t*)out_info + length;
  size_t name_length = strlen(name);
  if (((uint8_t*)&out_info->label[0]) + name_length > end) {
    return X_STATUS_BUFFER_OVERFLOW;
  }

  out_info->creation_time = 0;
  out_info->serial_number = 12345678;
  out_info->supports_objects = 0;
  out_info->label_length = (uint32_t)name_length;
  memcpy(out_info->label, name, name_length);
  return X_STATUS_SUCCESS;
}

// TODO(gibbed): call into HostPathDevice?
X_STATUS HostPathDevice::QueryFileSystemAttributes(XFileSystemAttributeInfo* out_info, size_t length) {
  assert_not_null(out_info);
  const char* name = "test"; // TODO(gibbed): actual value

  auto end = (uint8_t*)out_info + length;
  size_t name_length = strlen(name);
  if (((uint8_t*)&out_info->fs_name[0]) + name_length > end) {
    return X_STATUS_BUFFER_OVERFLOW;
  }

  out_info->attributes = 0;
  out_info->maximum_component_name_length = 255; // TODO(gibbed): actual value
  out_info->fs_name_length = (uint32_t)name_length;
  memcpy(out_info->fs_name, name, name_length);
  return X_STATUS_SUCCESS;
}
