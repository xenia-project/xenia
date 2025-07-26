/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_error.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace kernel {
namespace xam {

struct XTASK_MESSAGE {
  be<uint32_t> unknown_00;
  be<uint32_t> unknown_04;
  be<uint32_t> unknown_08;
  be<uint32_t> callback_arg_ptr;
  be<uint32_t> event_handle;
  be<uint32_t> unknown_14;
  be<uint32_t> task_handle;
};

struct XAM_TASK_ARGS {
  be<uint32_t> flags;
  be<uint32_t> value2;
  // i think there might be another value here, it might be padding
};
static_assert_size(XTASK_MESSAGE, 0x1C);

dword_result_t XamTaskSchedule_entry(lpvoid_t callback,
                                     pointer_t<XTASK_MESSAGE> message,
                                     dword_t optional_ptr, lpdword_t handle_ptr,
                                     const ppc_context_t& ctx) {
  // TODO(gibbed): figure out what this is for
  *handle_ptr = 12345;

  if (optional_ptr) {
    auto option = ctx->TranslateVirtual<XAM_TASK_ARGS*>(optional_ptr);

    auto v1 = option->flags;
    auto v2 = option->value2;  // typically 0?

    XELOGI("Got xam task args: v1 = {:08X}, v2 = {:08X}", v1.get(), v2.get());
  }

  uint32_t stack_size = kernel_state()->GetExecutableModule()->stack_size();

  // Stack must be aligned to 16kb pages
  stack_size = std::max((uint32_t)0x4000, ((stack_size + 0xFFF) & 0xFFFFF000));

  auto thread = object_ref<XThread>(new XThread(
      kernel_state(), stack_size, 0, callback, message.guest_address(), 0, true,
      false, kernel_state()->GetSystemProcess()));

  X_STATUS result = thread->Create();

  if (XFAILED(result)) {
    // Failed!
    XELOGE("XAM task creation failed: {:08X}", result);
    return result;
  }

  XELOGD("XAM task ({:08X}) scheduled asynchronously",
         callback.guest_address());

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT2(XamTaskSchedule, kNone, kImplemented, kSketchy);

dword_result_t XamTaskCloseHandle_entry(dword_t obj_handle) {
  const X_STATUS error_code = xboxkrnl::NtClose(obj_handle);

  if (XFAILED(error_code)) {
    const uint32_t result = xboxkrnl::xeRtlNtStatusToDosError(error_code);
    XThread::SetLastError(result);
    return false;
  }

  return true;
}
DECLARE_XAM_EXPORT1(XamTaskCloseHandle, kNone, kImplemented);

dword_result_t XamTaskShouldExit_entry() { return 0; }
DECLARE_XAM_EXPORT1(XamTaskShouldExit, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Task);
