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
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xbdm/xbdm_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"
//chrispy: no idea what a real valid value is for this
static constexpr const char DmXboxName[] = "Xbox360Name";
namespace xe {
  namespace kernel {
namespace xbdm {
#define XBDM_SUCCESSFULL 0x02DA0000
#define MAKE_DUMMY_STUB_PTR(x)             \
  dword_result_t x##_entry() { return 0; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

#define MAKE_DUMMY_STUB_STATUS(x)                                   \
  dword_result_t x##_entry() { return X_STATUS_INVALID_PARAMETER; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

MAKE_DUMMY_STUB_PTR(DmAllocatePool);

void DmCloseLoadedModules_entry(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT1(DmCloseLoadedModules, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmFreePool);

dword_result_t DmGetXbeInfo_entry() {
  // TODO(gibbed): 4D5307DC appears to expect this as success?
  // Unknown arguments -- let's hope things don't explode.
  return XBDM_SUCCESSFULL;
}
DECLARE_XBDM_EXPORT1(DmGetXbeInfo, kDebug, kStub);

dword_result_t DmGetXboxName_entry(const ppc_context_t& ctx) {
  uint64_t arg1 = ctx->r[3];
  uint64_t arg2 = ctx->r[4];
  if (!arg1 || !arg2) {
    return 0x80070057;
  }
  char* name_out = ctx->TranslateVirtualGPR<char*>(arg1);

  uint32_t* max_name_chars_ptr = ctx->TranslateVirtualGPR<uint32_t*>(arg2);

  uint32_t max_name_chars = xe::load_and_swap<uint32_t>(max_name_chars_ptr);
  strncpy(name_out, DmXboxName, sizeof(DmXboxName));


  return XBDM_SUCCESSFULL;
}
DECLARE_XBDM_EXPORT1(DmGetXboxName, kDebug, kImplemented)

dword_result_t DmIsDebuggerPresent_entry() { return 0; }
DECLARE_XBDM_EXPORT1(DmIsDebuggerPresent, kDebug, kStub);

void DmSendNotificationString_entry(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT1(DmSendNotificationString, kDebug, kStub);

dword_result_t DmRegisterCommandProcessor_entry(lpdword_t name_ptr,
                                                lpdword_t handler_fn) {
  // Return success to prevent some games from crashing
  return X_STATUS_SUCCESS;
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessor, kDebug, kStub);

dword_result_t DmRegisterCommandProcessorEx_entry(lpdword_t name_ptr,
                                                  lpdword_t handler_fn,
                                                  dword_t unk3) {
  // Return success to prevent some games from stalling
  return X_STATUS_SUCCESS;
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessorEx, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmStartProfiling);
MAKE_DUMMY_STUB_STATUS(DmStopProfiling);
// two arguments, first is num frames i think, second is some kind of pointer to
// where to capture
dword_result_t DmCaptureStackBackTrace_entry(const ppc_context_t& ctx) {
  uint32_t nframes = static_cast<uint32_t>(ctx->r[3]);
  uint8_t* unknown_addr =
      ctx->TranslateVirtual(static_cast<uint32_t>(ctx->r[4]));
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT1(DmCaptureStackBackTrace, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmGetThreadInfoEx);
MAKE_DUMMY_STUB_STATUS(DmSetProfilingOptions);

dword_result_t DmWalkLoadedModules_entry(lpdword_t unk0_ptr,
                                         lpdword_t unk1_ptr) {
  // Some games will loop forever unless this code is returned
  return 0x82DA0104;
}
DECLARE_XBDM_EXPORT1(DmWalkLoadedModules, kDebug, kStub);

void DmMapDevkitDrive_entry(const ppc_context_t& ctx) {
  // games check for nonzero result, failure if nz
  ctx->r[3] = 0ULL;
}
DECLARE_XBDM_EXPORT1(DmMapDevkitDrive, kDebug, kStub);

dword_result_t DmFindPdbSignature_entry(lpdword_t unk0_ptr,
                                        lpdword_t unk1_ptr) {
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT1(DmFindPdbSignature, kDebug, kStub);

dword_result_t DmGetConsoleDebugMemoryStatus_entry() {
  return X_STATUS_SUCCESS;
}
DECLARE_XBDM_EXPORT1(DmGetConsoleDebugMemoryStatus, kDebug, kStub);

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

DECLARE_XBDM_EMPTY_REGISTER_EXPORTS(Misc);
