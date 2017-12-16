/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xbdm/xbdm_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xbdm {

void DmCloseLoadedModules(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT(DmCloseLoadedModules, ExportTag::kStub | ExportTag::kDebug);

void DmSendNotificationString(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT(DmSendNotificationString,
                    ExportTag::kStub | ExportTag::kDebug);

dword_result_t DmWalkLoadedModules(lpdword_t unk0_ptr, lpdword_t unk1_ptr) {
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT(DmWalkLoadedModules, ExportTag::kStub | ExportTag::kDebug);

dword_result_t DmCaptureStackBackTrace(lpdword_t unk0_ptr, lpdword_t unk1_ptr) {
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT(DmCaptureStackBackTrace,
                    ExportTag::kStub | ExportTag::kDebug);

void DmMapDevkitDrive() {}
DECLARE_XBDM_EXPORT(DmMapDevkitDrive, ExportTag::kStub | ExportTag::kDebug);

dword_result_t DmFindPdbSignature(lpdword_t unk0_ptr, lpdword_t unk1_ptr) {
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT(DmFindPdbSignature, ExportTag::kStub | ExportTag::kDebug);

void RegisterMiscExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe
