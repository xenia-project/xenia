/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

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

struct DeviceInfo {
  uint32_t device_id;
  uint32_t device_type;
  uint64_t total_bytes;
  uint64_t free_bytes;
  char16_t name[28];
};

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
static const DeviceInfo dummy_device_info_ = {
    0x00000001,      // id
    1,               // 1=HDD
    20ull * ONE_GB,  // 20GB
    3ull * ONE_GB,   // 3GB, so it looks a little used.
    u"Dummy HDD",
};
#undef ONE_GB

dword_result_t XamContentGetDeviceName(dword_t device_id,
                                       lpu16string_t name_buffer,
                                       dword_t name_capacity) {
  if ((device_id & 0x0000000F) != dummy_device_info_.device_id) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  auto name = std::u16string(dummy_device_info_.name);
  if (name_capacity < name.size() + 1) {
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  xe::store_and_swap<std::u16string>(name_buffer, name);
  ((char16_t*)name_buffer)[name.size()] = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceName, kContent, kImplemented);

dword_result_t XamContentGetDeviceState(dword_t device_id,
                                        lpunknown_t overlapped_ptr) {
  if ((device_id & 0x0000000F) != dummy_device_info_.device_id) {
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
  xe::be<uint16_t> name[28];
} X_CONTENT_DEVICE_DATA;
static_assert_size(X_CONTENT_DEVICE_DATA, 0x50);

dword_result_t XamContentGetDeviceData(
    dword_t device_id, pointer_t<X_CONTENT_DEVICE_DATA> device_data) {
  if ((device_id & 0x0000000F) != dummy_device_info_.device_id) {
    // TODO(benvanik): memset 0 the data?
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  device_data.Zero();
  const auto& device_info = dummy_device_info_;
  device_data->device_id = device_info.device_id;
  device_data->device_type = device_info.device_type;
  device_data->total_bytes = device_info.total_bytes;
  device_data->free_bytes = device_info.free_bytes;
  xe::store_and_swap<std::u16string>(&device_data->name[0], device_info.name);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentGetDeviceData, kContent, kImplemented);

dword_result_t XamContentCreateDeviceEnumerator(dword_t content_type,
                                                dword_t content_flags,
                                                dword_t max_count,
                                                lpdword_t buffer_size_ptr,
                                                lpdword_t handle_out) {
  assert_not_null(handle_out);

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(DeviceInfo) * max_count;
  }

  auto e = new XStaticEnumerator(kernel_state(), max_count, sizeof(DeviceInfo));
  e->Initialize();

  // Copy our dummy device into the enumerator
  DeviceInfo* dev = (DeviceInfo*)e->AppendItem();
  if (dev) {
    xe::store_and_swap(&dev->device_id, dummy_device_info_.device_id);
    xe::store_and_swap(&dev->device_type, dummy_device_info_.device_type);
    xe::store_and_swap(&dev->total_bytes, dummy_device_info_.total_bytes);
    xe::store_and_swap(&dev->free_bytes, dummy_device_info_.free_bytes);
    xe::copy_and_swap(dev->name, dummy_device_info_.name,
                      xe::countof(dev->name));
  }

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentCreateDeviceEnumerator, kNone, kImplemented);

void RegisterContentDeviceExports(xe::cpu::ExportResolver* export_resolver,
                                  KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
