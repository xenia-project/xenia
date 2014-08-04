/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam_input.h>

#include <xenia/emulator.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::hid;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL XamVoiceCreate_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk1 = SHIM_GET_ARG_32(0); // 0
  uint32_t unk2 = SHIM_GET_ARG_32(1); // 0xF
  uint32_t out_voice_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "XamVoiceCreate(%.8X, %.8X, %.8X)",
      unk1, unk2, out_voice_ptr);

  // Null out the ptr.
  SHIM_SET_MEM_32(out_voice_ptr, 0);

  SHIM_SET_RETURN_32(X_ERROR_ACCESS_DENIED);
}


SHIM_CALL XamVoiceClose_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t voice_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "XamVoiceClose(%.8X)",
      voice_ptr);

  SHIM_SET_RETURN_32(0);
}


SHIM_CALL XamVoiceHeadsetPresent_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t voice_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "XamVoiceHeadsetPresent(%.8X)",
      voice_ptr);

  SHIM_SET_RETURN_32(0);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterVoiceExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamVoiceCreate, state);
  SHIM_SET_MAPPING("xam.xex", XamVoiceClose, state);
  SHIM_SET_MAPPING("xam.xex", XamVoiceHeadsetPresent, state);
}
