/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_INFO_VOLUME_H_
#define XENIA_KERNEL_INFO_VOLUME_H_

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

enum X_FILE_FS_INFORMATION_CLASS {
  XFileFsVolumeInformation = 1,
  XFileFsSizeInformation = 3,
  XFileFsDeviceInformation,
  XFileFsAttributeInformation = 5,
};

#pragma pack(push, 1)

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_fs_volume_information
struct X_FILE_FS_VOLUME_INFORMATION {
  be<uint64_t> creation_time;
  be<uint32_t> serial_number;
  be<uint32_t> label_length;
  uint8_t supports_objects;
  char label[1];
  uint8_t pad[6];
};
static_assert_size(X_FILE_FS_VOLUME_INFORMATION, 24);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_fs_size_information
struct X_FILE_FS_SIZE_INFORMATION {
  be<uint64_t> total_allocation_units;
  be<uint64_t> available_allocation_units;
  be<uint32_t> sectors_per_allocation_unit;
  be<uint32_t> bytes_per_sector;
};
static_assert_size(X_FILE_FS_SIZE_INFORMATION, 24);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_fs_attribute_information
struct X_FILE_FS_ATTRIBUTE_INFORMATION {
  be<uint32_t> attributes;
  be<int32_t> component_name_max_length;
  be<uint32_t> name_length;
  char name[1];
  uint8_t pad[3];
};
static_assert_size(X_FILE_FS_ATTRIBUTE_INFORMATION, 16);

enum X_FILE_DEVICE_TYPE : uint32_t {
	FILE_DEVICE_UNKNOWN = 0x22
};

struct X_FILE_FS_DEVICE_INFORMATION {
  be<X_FILE_DEVICE_TYPE> device_type;
  be<uint32_t> characteristics;
};
static_assert_size(X_FILE_FS_DEVICE_INFORMATION, 8);

#pragma pack(pop)

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_INFO_VOLUME_H_
