/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

DECLARE_uint32(audio_flag);

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XAudioGetSpeakerConfig_entry(lpdword_t config_ptr) {
  *config_ptr = cvars::audio_flag;
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetSpeakerConfig, kAudio, kImplemented);

dword_result_t XAudioGetVoiceCategoryVolumeChangeMask_entry(
    lpunknown_t driver_ptr, lpdword_t out_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  xe::threading::MaybeYield();

  // Checking these bits to see if any voice volume changed.
  // I think.
  *out_ptr = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioGetVoiceCategoryVolumeChangeMask, kAudio, kStub,
                         kHighFrequency);

dword_result_t XAudioGetVoiceCategoryVolume_entry(dword_t unk,
                                                  lpfloat_t out_ptr) {
  // Expects a floating point single. Volume %?
  *out_ptr = 1.0f;

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioGetVoiceCategoryVolume, kAudio, kStub,
                         kHighFrequency);

dword_result_t XAudioEnableDucker_entry(dword_t unk) { return X_ERROR_SUCCESS; }
DECLARE_XBOXKRNL_EXPORT1(XAudioEnableDucker, kAudio, kStub);

dword_result_t XAudioRegisterRenderDriverClient_entry(lpdword_t callback_ptr,
                                                      lpdword_t driver_ptr) {
  if (!callback_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t callback = callback_ptr[0];

  if (!callback) {
    return X_E_INVALIDARG;
  }
  uint32_t callback_arg = callback_ptr[1];

  auto audio_system = kernel_state()->emulator()->audio_system();

  size_t index;
  auto result = audio_system->RegisterClient(callback, callback_arg, &index);
  if (XFAILED(result)) {
    return result;
  }

  assert_true(!(index & ~0x0000FFFF));
  *driver_ptr = 0x41550000 | (static_cast<uint32_t>(index) & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioRegisterRenderDriverClient, kAudio,
                         kImplemented);

dword_result_t XAudioUnregisterRenderDriverClient_entry(
    lpunknown_t driver_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->UnregisterClient(driver_ptr.guest_address() & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioUnregisterRenderDriverClient, kAudio,
                         kImplemented);

dword_result_t XAudioSubmitRenderDriverFrame_entry(lpunknown_t driver_ptr,
                                                   lpunknown_t samples_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  auto samples =
      kernel_state()->memory()->TranslateVirtual<float*>(samples_ptr);
  audio_system->SubmitFrame(driver_ptr.guest_address() & 0x0000FFFF, samples);

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioSubmitRenderDriverFrame, kAudio, kImplemented,
                         kHighFrequency);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Audio);
