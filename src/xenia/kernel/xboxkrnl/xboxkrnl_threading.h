/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
struct X_KEVENT;

namespace xboxkrnl {

dword_result_t NtSetEvent(dword_t handle, lpdword_t previous_state_ptr);
dword_result_t NtClearEvent(dword_t handle);

dword_result_t NtWaitForMultipleObjectsEx(dword_t count, lpdword_t handles,
                                          dword_t wait_type, dword_t wait_mode,
                                          dword_t alertable,
                                          lpqword_t timeout_ptr);

dword_result_t KeWaitForSingleObject(lpvoid_t object_ptr, dword_t wait_reason,
                                     dword_t processor_mode, dword_t alertable,
                                     lpqword_t timeout_ptr);
dword_result_t KeSetEvent(pointer_t<X_KEVENT> event_ptr, dword_t increment,
                          dword_t wait);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_THREADING_H_
