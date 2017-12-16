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

void DbgBreakPoint() { xe::debugging::Break(); }
DECLARE_XBOXKRNL_EXPORT(DbgBreakPoint, ExportTag::kImportant);

// https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
typedef struct {
  xe::be<uint32_t> type;
  xe::be<uint32_t> name_ptr;
  xe::be<uint32_t> thread_id;
  xe::be<uint32_t> flags;
} X_THREADNAME_INFO;
static_assert_size(X_THREADNAME_INFO, 0x10);

void RtlRaiseException(pointer_t<X_EXCEPTION_RECORD> record) {
  if (record->exception_code == 0x406D1388) {
    // SetThreadName. FFS.
    // https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx

    // TODO(benvanik): check record->number_parameters to make sure it's a
    // correct size.
    auto thread_info =
        reinterpret_cast<X_THREADNAME_INFO*>(&record->exception_information[0]);

    assert_true(thread_info->type == 0x1000);
    auto name =
        kernel_memory()->TranslateVirtual<const char*>(thread_info->name_ptr);

    object_ref<XThread> thread;
    if (thread_info->thread_id == -1) {
      // Current thread.
      thread = retain_object(XThread::GetCurrentThread());
    } else {
      // Lookup thread by ID.
      thread = kernel_state()->GetThreadByID(thread_info->thread_id);
    }

    if (thread) {
      XELOGD("SetThreadName(%d, %s)", thread->thread_id(), name);
      thread->set_name(name);
    }

    // TODO(benvanik): unwinding required here?
    return;
  }

  if (record->exception_code == 0xE06D7363) {
    // C++ exception.
    // http://blogs.msdn.com/b/oldnewthing/archive/2010/07/30/10044061.aspx
    xe::debugging::Break();
    return;
  }

  // TODO(benvanik): unwinding.
  // This is going to suck.
  xe::debugging::Break();
}
DECLARE_XBOXKRNL_EXPORT(RtlRaiseException, ExportTag::kImportant);

void KeBugCheckEx(dword_t code, dword_t param1, dword_t param2, dword_t param3,
                  dword_t param4) {
  XELOGD("*** STOP: 0x%.8X (0x%.8X, 0x%.8X, 0x%.8X, 0x%.8X)", code, param1,
         param2, param3, param4);
  fflush(stdout);
  xe::debugging::Break();
  assert_always();
}
DECLARE_XBOXKRNL_EXPORT(KeBugCheckEx, ExportTag::kImportant);

void KeBugCheck(dword_t code) { KeBugCheckEx(code, 0, 0, 0, 0); }
DECLARE_XBOXKRNL_EXPORT(KeBugCheck, ExportTag::kImportant);

void RegisterDebugExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
