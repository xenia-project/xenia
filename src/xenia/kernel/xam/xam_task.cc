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
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
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

constexpr uint32_t kCachePartitionMagic = 0x4A6F7368;  // 'Josh'

struct XTASK_MESSAGE {
  be<uint32_t> unknown_00;
  be<uint32_t> unknown_04;
  be<uint32_t> unknown_08;
  be<uint32_t> callback_arg_ptr;
  be<uint32_t> event_handle;
  be<uint32_t> unknown_14;
  be<uint32_t> task_handle;
};
static_assert_size(XTASK_MESSAGE, 0x1C);

bool cache_task_scheduled_ = false;

dword_result_t XamTaskSchedule(lpvoid_t callback,
                               pointer_t<XTASK_MESSAGE> message,
                               lpdword_t unknown, lpdword_t handle_ptr) {
  // TODO(gibbed): figure out what this is for
  *handle_ptr = 12345;

  XELOGW("!! Executing scheduled task ({:08X}) synchronously, PROBABLY BAD !! ",
         callback.guest_address());

  // TODO(gibbed): this is supposed to be async... let's cheat.
  auto thread_state = XThread::GetCurrentThread()->thread_state();
  uint64_t args[] = {message.guest_address()};
  auto result = kernel_state()->processor()->Execute(thread_state, callback,
                                                     args, xe::countof(args));

  // Check if unknown param matches what XMountUtilityDrive uses
  // (these are likely flags instead of an ID though, maybe has a chance of
  // being used by something other than XMountUtilityDrive...)
  if (unknown && *unknown == 0x2080002) {
    // If this is cache-partition-task game will set message[0x10 or 0x14] to
    // 0x4A6F7368 ('Josh'), offset probably changes depending on revision of
    // cache-mounting code?

    if (message->unknown_14 == kCachePartitionMagic) {
      // Later revision of cache-partition code
      // Result at message[0x10]
      message->event_handle = X_STATUS_SUCCESS;

      // Remember that cache was mounted so other code can act accordingly
      // TODO: make sure to reset this when emulation starts!
      cache_task_scheduled_ = true;
    } else if (message->event_handle == kCachePartitionMagic) {
      // Earlier cache code
      // Result at message[0xC]
      message->callback_arg_ptr = X_STATUS_SUCCESS;

      // Remember that cache was mounted so other code can act accordingly
      // TODO: make sure to reset this when emulation starts!
      cache_task_scheduled_ = true;
    }
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT2(XamTaskSchedule, kNone, kImplemented, kSketchy);

void RegisterTaskExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
