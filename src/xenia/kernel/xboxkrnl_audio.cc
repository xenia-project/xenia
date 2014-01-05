/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_audio.h>

#include <xenia/apu/apu.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {


SHIM_CALL XMACreateContext_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "XMACreateContext(%.8X)",
      context_ptr);

  // TODO(benvanik): allocate and return -- see if size required or just dummy?

  SHIM_SET_RETURN(X_ERROR_ACCESS_DENIED);
}


SHIM_CALL XMAReleaseContext_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "XMAReleaseContext(%.8X)",
      context_ptr);

  // TODO(benvanik): free
}


SHIM_CALL XAudioGetVoiceCategoryVolume_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk_0 = SHIM_GET_ARG_32(0);
  uint32_t out_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XAudioGetVoiceCategoryVolume(%.8X, %.8X)",
      unk_0, out_ptr);

  // Expects a floating point single. Volume %?
  SHIM_SET_MEM_32(out_ptr, 0);

  SHIM_SET_RETURN(X_ERROR_SUCCESS);
}


SHIM_CALL XAudioGetSpeakerConfig_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t config_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "XAudioGetSpeakerConfig(%.8X)",
      config_ptr);

  SHIM_SET_MEM_32(config_ptr, 1); // ?

  SHIM_SET_RETURN(X_ERROR_SUCCESS);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterAudioExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", XMACreateContext, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAInitializeContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAReleaseContext, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAEnableContext, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMADisableContext, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferWriteOffset, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferReadOffset, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferReadOffset, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferValid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsOutputBufferValid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0Valid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer0Valid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1Valid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer1Valid, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetPacketMetadata, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMABlockWhileInUse, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetLoopData, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBufferReadOffset, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetInputBufferReadOffset, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XAudioGetVoiceCategoryVolume, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XAudioGetSpeakerConfig, state);
}
