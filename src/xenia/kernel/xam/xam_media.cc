/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamMediaVerificationCreate_entry(dword_t creation_flags) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamMediaVerificationCreate, kNone, kStub);

dword_result_t XamMediaVerificationClose_entry() { return X_ERROR_SUCCESS; }
DECLARE_XAM_EXPORT1(XamMediaVerificationClose, kNone, kStub);

dword_result_t XamMediaVerificationVerify_entry(
    dword_t r3, pointer_t<XAM_OVERLAPPED> overlapped, lpvoid_t callback) {
  if (callback) {
    auto thread = XThread::GetCurrentThread();
    thread->EnqueueApc(static_cast<uint32_t>(callback) & ~1u, 0, 0, 0);
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamMediaVerificationVerify, kNone, kStub);

struct X_SECURITY_FAILURE_INFORMATION {
  xe::be<uint32_t> size;
  xe::be<uint32_t> failed_reads;
  xe::be<uint32_t> failed_hashes;
  xe::be<uint32_t> blocks_checked;
  xe::be<uint32_t> total_blocks;
  xe::be<uint32_t> complete;
};

dword_result_t XamMediaVerificationFailedBlocks_entry(
    pointer_t<X_SECURITY_FAILURE_INFORMATION> failure_information) {
  if (failure_information) {
    if (failure_information->size != sizeof(X_SECURITY_FAILURE_INFORMATION)) {
      return X_ERROR_NOT_ENOUGH_MEMORY;
    }

    failure_information->failed_reads = 0;
    failure_information->failed_hashes = 0;
    failure_information->blocks_checked = 0;
    failure_information->total_blocks = 0;
    failure_information->complete = true;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamMediaVerificationFailedBlocks, kNone, kStub);

dword_result_t XamMediaVerificationInject_entry(dword_t r3, dword_t r4) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamMediaVerificationInject, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Media);