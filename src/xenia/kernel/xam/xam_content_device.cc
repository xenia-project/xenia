/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_content_device.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// TODO(gibbed): real information.
//
// Until we expose real information about a HDD device, we
// claim there is 3GB free on a 4GB dummy HDD.
//
// There is a possibility that certain games are bugged in that
// they incorrectly only look at the lower 32-bits of free_bytes,
// when it is a 64-bit value. Which means any size above ~4GB
// will not be recognized properly.
#define ONE_GB (1024ull * 1024ull * 1024ull)

static const DummyDeviceInfo dummy_hdd_device_info_ = {
    DummyDeviceId::HDD, DeviceType::HDD,
    20ull * ONE_GB,  // 20GB
    3ull * ONE_GB,   // 3GB, so it looks a little used.
    u"Dummy HDD",
};
static const DummyDeviceInfo dummy_odd_device_info_ = {
    DummyDeviceId::ODD, DeviceType::ODD,
    7ull * ONE_GB,  // 7GB (rough maximum)
    0ull * ONE_GB,  // read-only FS, so no free space
    u"Dummy ODD",
};
static const DummyDeviceInfo* dummy_device_infos_[] = {
    &dummy_hdd_device_info_,
    &dummy_odd_device_info_,
};
#undef ONE_GB

const DummyDeviceInfo* GetDummyDeviceInfo(uint32_t device_id) {
  const auto& begin = std::begin(dummy_device_infos_);
  const auto& end = std::end(dummy_device_infos_);
  auto it = std::find_if(begin, end, [device_id](const auto& item) {
    return static_cast<uint32_t>(item->device_id) == device_id;
  });
  return it == end ? nullptr : *it;
}

std::vector<const DummyDeviceInfo*> ListStorageDevices(bool include_readonly) {
  // FIXME: Should probably check content flags here instead.
  std::vector<const DummyDeviceInfo*> devices;

  for (const auto& device_info : dummy_device_infos_) {
    if (!include_readonly && device_info->device_type == DeviceType::ODD) {
      continue;
    }
    devices.emplace_back(device_info);
  }

  return devices;
}

dword_result_t XamContentGetDeviceName_entry(dword_t device_id,
                                             dword_t name_buffer_ptr,
                                             dword_t name_capacity) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  auto name = std::u16string(device_info->name);
  if (name_capacity < name.size() + 1) {
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  char16_t* name_buffer =
      kernel_memory()->TranslateVirtual<char16_t*>(name_buffer_ptr);

  xe::string_util::copy_and_swap_truncating(name_buffer, name, name_capacity);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceName, kContent, kImplemented);

dword_result_t XamContentGetDeviceState_entry(dword_t device_id,
                                              lpunknown_t overlapped_ptr) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped_ptr, X_ERROR_FUNCTION_FAILED, X_ERROR_DEVICE_NOT_CONNECTED,
          0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
  }
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceState, kContent, kStub);

typedef struct {
  xe::be<uint32_t> device_id;
  xe::be<uint32_t> device_type;
  xe::be<uint64_t> total_bytes;
  xe::be<uint64_t> free_bytes;
  union {
    xe::be<uint16_t> name[28];
    char16_t name_chars[28];
  };
} X_CONTENT_DEVICE_DATA;
static_assert_size(X_CONTENT_DEVICE_DATA, 0x50);

dword_result_t XamContentGetDeviceData_entry(
    dword_t device_id, pointer_t<X_CONTENT_DEVICE_DATA> device_data) {
  auto device_info = GetDummyDeviceInfo(device_id);
  if (device_info == nullptr) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  device_data.Zero();
  device_data->device_id = static_cast<uint32_t>(device_info->device_id);
  device_data->device_type = static_cast<uint32_t>(device_info->device_type);
  device_data->total_bytes = device_info->total_bytes;
  device_data->free_bytes = device_info->free_bytes;
  xe::string_util::copy_and_swap_truncating(
      device_data->name_chars, device_info->name,
      xe::countof(device_data->name_chars));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceData, kContent, kImplemented);

dword_result_t XamContentCreateDeviceEnumerator_entry(dword_t content_type,
                                                      dword_t content_flags,
                                                      dword_t max_count,
                                                      lpdword_t buffer_size_ptr,
                                                      lpdword_t handle_out) {
  assert_not_null(handle_out);

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(X_CONTENT_DEVICE_DATA) * max_count;
  }

  auto e = make_object<XStaticEnumerator<X_CONTENT_DEVICE_DATA>>(kernel_state(),
                                                                 max_count);
  auto result = e->Initialize(XUserIndexNone, 0xFE, 0x2000A, 0x20009, 0);
  if (XFAILED(result)) {
    return result;
  }

  for (const auto& device_info : dummy_device_infos_) {
    // Copy our dummy device into the enumerator
    auto device_data = e->AppendItem();
    assert_not_null(device_data);
    if (device_data) {
      device_data->device_id = static_cast<uint32_t>(device_info->device_id);
      device_data->device_type =
          static_cast<uint32_t>(device_info->device_type);
      device_data->total_bytes = device_info->total_bytes;
      device_data->free_bytes = device_info->free_bytes;
      xe::string_util::copy_and_swap_truncating(
          device_data->name_chars, device_info->name,
          xe::countof(device_data->name_chars));
    }
  }

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentCreateDeviceEnumerator, kNone, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(ContentDevice);
