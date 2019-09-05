/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t KeCreateUserMode(lpvoid_t unk0, lpvoid_t unk1) {
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(KeCreateUserMode, kNone, kStub);

dword_result_t KeDeleteUserMode(lpvoid_t unk0) {
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(KeDeleteUserMode, kNone, kStub);

dword_result_t KeEnterUserMode(lpvoid_t start_context, lpvoid_t start_address,
                               lpvoid_t unk2,
                               lpvoid_t unk3) {
  
    /*auto thread = object_ref<XThread>(
      new XThread(kernel_state(), actual_stack_size, xapi_thread_startup,
                  start_address, start_context,
                  creation_flags, true));*/
  
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(KeEnterUserMode, kNone, kStub);

dword_result_t KeLeaveUserMode(lpvoid_t unk0) {
  // SHOULD NOT RETURN. Directly kills user mode code (thread?).
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(KeLeaveUserMode, kNone, kStub);

dword_result_t KeFlushUserModeTb(lpvoid_t unk0) {
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(KeFlushUserModeTb, kNone, kStub);

uint32_t UserModeReadRegisterThunk(void* ppc_context, KernelState* kernel_state,
                                   uint32_t addr) {
  return 0;
}

void UserModeWriteRegisterThunk(void* ppc_context, KernelState* kernel_state,
                                uint32_t addr, uint32_t value) {}

void RegisterUserModeExports(xe::cpu::ExportResolver* export_resolver,
                             KernelState* kernel_state) {
  /*kernel_state->memory()->AddVirtualMappedRange(
      0x3FC00000, 0xFFC00000, 0x003FFFFF, kernel_state,
      reinterpret_cast<cpu::MMIOReadCallback>(UserModeReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(UserModeWriteRegisterThunk));*/
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
