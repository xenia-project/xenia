/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

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
  be<uint32_t> value1;
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

    auto v1 = option->value1;
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

dword_result_t XamTaskShouldExit_entry(dword_t r3) { return 0; }
DECLARE_XAM_EXPORT2(XamTaskShouldExit, kNone, kStub, kSketchy);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Task);
