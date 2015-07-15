/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

typedef struct {
  xe::be<uint32_t> count;
  xe::be<uint32_t> state[5];
  uint8_t buffer[64];
} XECRYPT_SHA_STATE;

void XeCryptShaInit(pointer_t<XECRYPT_SHA_STATE> sha_state) {
  sha_state.Zero();
}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaInit, ExportTag::kStub);

void XeCryptShaUpdate(pointer_t<XECRYPT_SHA_STATE> sha_state, lpvoid_t input,
                      dword_t input_size) {}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaUpdate, ExportTag::kStub);

void XeCryptShaFinal(pointer_t<XECRYPT_SHA_STATE> sha_state, lpvoid_t out,
                     dword_t out_size) {}
DECLARE_XBOXKRNL_EXPORT(XeCryptShaFinal, ExportTag::kStub);

void xe::kernel::xboxkrnl::RegisterCryptExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {}

}  // namespace kernel
}  // namespace xe
