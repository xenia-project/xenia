/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L518
uint32_t xeXamEnumerate(uint32_t handle, uint32_t flags, lpvoid_t buffer_ptr,
                        uint32_t buffer_size, uint32_t* items_returned,
                        uint32_t overlapped_ptr) {
  assert_true(flags == 0);

  X_RESULT result;
  uint32_t item_count = 0;

  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    result = X_ERROR_INVALID_HANDLE;
  } else {
    size_t needed_buffer_size = e->item_size() * e->items_per_enumerate();

    uint32_t actual_buffer_size = buffer_size;
    if (buffer_size == e->items_per_enumerate()) {
      actual_buffer_size = static_cast<uint32_t>(needed_buffer_size);
      // Known culprits:
      //   Final Fight: Double Impact (saves)
      XELOGW(
          "Broken usage of XamEnumerate! buffer size={:X} vs actual "
          "size={:X} (item size={:X}, items per enumerate={})",
          buffer_size, actual_buffer_size, e->item_size(),
          e->items_per_enumerate());
    }

    result = X_ERROR_INSUFFICIENT_BUFFER;
    if (actual_buffer_size >= needed_buffer_size) {
      buffer_ptr.Zero(actual_buffer_size);
      result =
          e->WriteItems(buffer_ptr.guest_address(), buffer_ptr.as<uint8_t*>(),
                        actual_buffer_size, &item_count);
    }
  }

  if (items_returned) {
    assert_true(!overlapped_ptr);
    *items_returned = result == X_ERROR_SUCCESS ? item_count : 0;
    return result;
  } else if (overlapped_ptr) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped_ptr,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result),
        result == X_ERROR_SUCCESS ? item_count : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}

dword_result_t XamEnumerate(dword_t handle, dword_t flags, lpvoid_t buffer,
                            dword_t buffer_length, lpdword_t items_returned,
                            pointer_t<XAM_OVERLAPPED> overlapped) {
  uint32_t dummy;
  auto result = xeXamEnumerate(handle, flags, buffer, buffer_length,
                               !overlapped ? &dummy : nullptr, overlapped);
  if (!overlapped && items_returned) {
    *items_returned = dummy;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamEnumerate, kNone, kImplemented);

dword_result_t XamCreateEnumeratorHandle(unknown_t unk1, unknown_t unk2,
                                         unknown_t unk3, unknown_t unk4,
                                         unknown_t unk5, unknown_t unk6,
                                         unknown_t unk7, unknown_t unk8) {
  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamCreateEnumeratorHandle, kNone, kStub);

dword_result_t XamGetPrivateEnumStructureFromHandle(dword_t handle,
                                                    lpdword_t out_object_ptr) {
  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    return X_STATUS_INVALID_HANDLE;
  }

  // Caller takes the reference.
  // It's released in ObDereferenceObject.
  e->RetainHandle();

  if (out_object_ptr.guest_address()) {
    *out_object_ptr = e->guest_object();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetPrivateEnumStructureFromHandle, kNone, kStub);

void RegisterEnumExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
