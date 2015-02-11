/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/common.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xenumerator.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

struct DeviceInfo {
  uint32_t device_id;
  uint32_t device_type;
  uint64_t total_bytes;
  uint64_t free_bytes;
  std::wstring name;
};
static const DeviceInfo dummy_device_info_ = {
    0xF00D0000, 1, 1024 * 1024 * 1024, 1024 * 1024 * 1024, L"Dummy HDD",
};

SHIM_CALL XamContentGetLicenseMask_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t mask_ptr = SHIM_GET_ARG_32(0);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XamContentGetLicenseMask(%.8X, %.8X)", mask_ptr, overlapped_ptr);

  // Arcade games seem to call this and check the result mask for random bits.
  // If we fail, the games seem to use a hardcoded mask, which is likely trial.
  // To be clever, let's just try setting all the bits.
  SHIM_SET_MEM_32(mask_ptr, 0xFFFFFFFF);

  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamShowDeviceSelectorUI_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t content_type = SHIM_GET_ARG_32(1);
  uint32_t content_flags = SHIM_GET_ARG_32(2);
  uint64_t total_requested = SHIM_GET_ARG_64(3);
  uint32_t device_id_ptr = SHIM_GET_ARG_32(4);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(5);

  XELOGD("XamShowDeviceSelectorUI(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
         user_index, content_type, content_flags, total_requested,
         device_id_ptr, overlapped_ptr);

  switch (content_type) {
    case 1:  // save game
      SHIM_SET_MEM_32(device_id_ptr, dummy_device_info_.device_id | 0x0001);
      break;
    case 2:  // marketplace
      SHIM_SET_MEM_32(device_id_ptr, dummy_device_info_.device_id | 0x0002);
      break;
    case 3:  // title/publisher update?
      SHIM_SET_MEM_32(device_id_ptr, dummy_device_info_.device_id | 0x0003);
      break;
  }

  X_RESULT result = X_ERROR_SUCCESS;
  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, result);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(result);
  }
}

SHIM_CALL XamContentGetDeviceName_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t device_id = SHIM_GET_ARG_32(0);
  uint32_t name_ptr = SHIM_GET_ARG_32(1);
  uint32_t name_capacity = SHIM_GET_ARG_32(2);

  XELOGD("XamContentGetDeviceName(%.8X, %.8X, %d)", device_id, name_ptr,
         name_capacity);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }

  if (name_capacity < dummy_device_info_.name.size() + 1) {
    SHIM_SET_RETURN_32(X_ERROR_INSUFFICIENT_BUFFER);
    return;
  }

  poly::store_and_swap<std::wstring>(SHIM_MEM_ADDR(name_ptr),
                                     dummy_device_info_.name);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamContentGetDeviceState_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t device_id = SHIM_GET_ARG_32(0);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XamContentGetDeviceState(%.8X, %.8X)", device_id, overlapped_ptr);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    if (overlapped_ptr) {
      state->CompleteOverlappedImmediate(overlapped_ptr,
                                         X_ERROR_FUNCTION_FAILED);
      XOverlappedSetExtendedError(SHIM_MEM_BASE + overlapped_ptr,
                                  X_ERROR_DEVICE_NOT_CONNECTED);
      SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    }
    return;
  }

  if (overlapped_ptr) {
    state->CompleteOverlappedImmediate(overlapped_ptr, X_ERROR_SUCCESS);
    SHIM_SET_RETURN_32(X_ERROR_IO_PENDING);
  } else {
    SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
  }
}

SHIM_CALL XamContentGetDeviceData_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t device_id = SHIM_GET_ARG_32(0);
  uint32_t device_data_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XamContentGetDeviceData(%.8X, %.8X)", device_id, device_data_ptr);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    // TODO(benvanik): memset 0 the data?
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }

  const auto& device_info = dummy_device_info_;
  SHIM_SET_MEM_32(device_data_ptr + 0, device_info.device_id);
  SHIM_SET_MEM_32(device_data_ptr + 4, device_info.device_type);
  SHIM_SET_MEM_64(device_data_ptr + 8, device_info.total_bytes);
  SHIM_SET_MEM_64(device_data_ptr + 16, device_info.free_bytes);
  poly::store_and_swap<std::wstring>(SHIM_MEM_ADDR(device_data_ptr + 24),
                                     device_info.name);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

// http://gameservice.googlecode.com/svn-history/r14/trunk/ContentManager.cpp
SHIM_CALL XamContentCreateEnumerator_shim(PPCContext* ppc_state,
                                          KernelState* state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t device_id = SHIM_GET_ARG_32(1);
  uint32_t content_type = SHIM_GET_ARG_32(2);
  uint32_t content_flags = SHIM_GET_ARG_32(3);
  uint32_t item_count = SHIM_GET_ARG_32(4);
  uint32_t buffer_size_ptr = SHIM_GET_ARG_32(5);
  uint32_t handle_ptr = SHIM_GET_ARG_32(6);

  XELOGD("XamContentCreateEnumerator(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
         user_index, device_id, content_type, content_flags, item_count,
         buffer_size_ptr, handle_ptr);

  if (device_id && (device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    // TODO(benvanik): memset 0 the data?
    SHIM_SET_RETURN_32(X_E_INVALIDARG);
    return;
  }

  // 4 + 4 + 128*2 + 42 = 306
  if (buffer_size_ptr) {
    uint32_t bp = SHIM_MEM_32(buffer_size_ptr);
    SHIM_SET_MEM_32(buffer_size_ptr, item_count * 306);
  }

  XEnumerator* e = new XEnumerator(state);
  e->Initialize();

  SHIM_SET_MEM_32(handle_ptr, e->handle());

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterContentExports(ExportResolver* export_resolver,
                                             KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamContentGetLicenseMask, state);
  SHIM_SET_MAPPING("xam.xex", XamShowDeviceSelectorUI, state);
  SHIM_SET_MAPPING("xam.xex", XamContentGetDeviceName, state);
  SHIM_SET_MAPPING("xam.xex", XamContentGetDeviceState, state);
  SHIM_SET_MAPPING("xam.xex", XamContentGetDeviceData, state);
  SHIM_SET_MAPPING("xam.xex", XamContentCreateEnumerator, state);
}
