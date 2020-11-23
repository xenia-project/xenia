/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L518
dword_result_t XamEnumerate(dword_t handle, dword_t flags, lpvoid_t buffer,
                            dword_t buffer_length, lpdword_t items_returned,
                            pointer_t<XAM_OVERLAPPED> overlapped) {
  assert_true(flags == 0);

  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped, X_ERROR_INVALID_HANDLE, X_ERROR_INVALID_HANDLE, 0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_INVALID_HANDLE;
    }
  }

  size_t actual_buffer_length = (uint32_t)buffer_length;
  if (buffer_length == e->items_per_enumerate()) {
    actual_buffer_length = e->item_size() * e->items_per_enumerate();
    // Known culprits:
    //   Final Fight: Double Impact (saves)
    XELOGW(
        "Broken usage of XamEnumerate! buffer length={:X} vs actual "
        "length={:X} "
        "(item size={:X}, items per enumerate={})",
        (uint32_t)buffer_length, actual_buffer_length, e->item_size(),
        e->items_per_enumerate());
  }

  buffer.Zero(actual_buffer_length);

  X_RESULT result;
  uint32_t item_count = 0;

  if (actual_buffer_length < e->item_size()) {
    result = X_ERROR_INSUFFICIENT_BUFFER;
  } else if (e->current_item() >= e->item_count()) {
    result = X_ERROR_NO_MORE_FILES;
  } else {
    auto item_buffer = buffer.as<uint8_t*>();
    auto max_items = actual_buffer_length / e->item_size();
    while (max_items--) {
      if (!e->WriteItem(item_buffer)) {
        break;
      }
      item_buffer += e->item_size();
      item_count++;
    }
    result = X_ERROR_SUCCESS;
  }

  if (items_returned) {
    assert_true(!overlapped);
    *items_returned = result == X_ERROR_SUCCESS ? item_count : 0;
    return result;
  } else if (overlapped) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result),
        result == X_ERROR_SUCCESS ? item_count : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT1(XamEnumerate, kNone, kImplemented);

dword_result_t XamCreateEnumeratorHandle(unknown_t unk1, unknown_t unk2,
                                         unknown_t unk3, unknown_t unk4,
                                         unknown_t unk5, unknown_t unk6,
                                         unknown_t unk7, unknown_t unk8) {
  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamCreateEnumeratorHandle, kNone, kStub);

dword_result_t XamGetPrivateEnumStructureFromHandle(unknown_t unk1,
                                                    unknown_t unk2) {
  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamGetPrivateEnumStructureFromHandle, kNone, kStub);

void RegisterEnumExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
