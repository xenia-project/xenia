/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/device.h"

#include "xenia/kernel/objects/xfile.h"

namespace xe {
namespace vfs {

Device::Device(const std::string& mount_path) : mount_path_(mount_path) {}

Device::~Device() = default;

// TODO(gibbed): make virtual + move implementation into HostPathDevice/etc.
X_STATUS Device::QueryVolumeInfo(X_FILE_FS_VOLUME_INFORMATION* out_info,
                                 size_t length) {
  assert_not_null(out_info);
  const char* name = "test";  // TODO(gibbed): actual value

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

// TODO(gibbed): make virtual + move implementation into HostPathDevice/etc.
X_STATUS Device::QuerySizeInfo(X_FILE_FS_SIZE_INFORMATION* out_info,
                               size_t length) {
  assert_not_null(out_info);
  out_info->total_allocation_units = 1234;    // TODO(gibbed): actual value
  out_info->available_allocation_units = 0;   // TODO(gibbed): actual value
  out_info->sectors_per_allocation_unit = 1;  // TODO(gibbed): actual value
  out_info->bytes_per_sector = 1024;          // TODO(gibbed): actual value
  return X_STATUS_SUCCESS;
}

// TODO(gibbed): make virtual + move implementation into HostPathDevice/etc.
X_STATUS Device::QueryAttributeInfo(X_FILE_FS_ATTRIBUTE_INFORMATION* out_info,
                                    size_t length) {
  assert_not_null(out_info);
  const char* name = "test";  // TODO(gibbed): actual value

  auto end = (uint8_t*)out_info + length;
  size_t name_length = strlen(name);
  if (((uint8_t*)&out_info->fs_name[0]) + name_length > end) {
    return X_STATUS_BUFFER_OVERFLOW;
  }

  out_info->attributes = 0;
  out_info->maximum_component_name_length = 255;  // TODO(gibbed): actual value
  out_info->fs_name_length = (uint32_t)name_length;
  memcpy(out_info->fs_name, name, name_length);
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
