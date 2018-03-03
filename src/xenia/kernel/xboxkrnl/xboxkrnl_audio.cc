/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
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

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XAudioGetSpeakerConfig(lpdword_t config_ptr) {
  *config_ptr = 0x00010001;
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XAudioGetSpeakerConfig,
                        ExportTag::kImplemented | ExportTag::kAudio);

dword_result_t XAudioGetVoiceCategoryVolumeChangeMask(lpunknown_t driver_ptr,
                                                      lpdword_t out_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  // Checking these bits to see if any voice volume changed.
  // I think.
  *out_ptr = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XAudioGetVoiceCategoryVolumeChangeMask,
                        ExportTag::kStub | ExportTag::kAudio);

dword_result_t XAudioGetVoiceCategoryVolume(dword_t unk, lpfloat_t out_ptr) {
  // Expects a floating point single. Volume %?
  *out_ptr = 1.0f;

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XAudioGetVoiceCategoryVolume,
                        ExportTag::kStub | ExportTag::kAudio);

dword_result_t XAudioEnableDucker(dword_t unk) { return X_ERROR_SUCCESS; }
DECLARE_XBOXKRNL_EXPORT(XAudioEnableDucker,
                        ExportTag::kStub | ExportTag::kAudio);

dword_result_t XAudioRegisterRenderDriverClient(lpdword_t callback_ptr,
                                                lpdword_t driver_ptr) {
  uint32_t callback = callback_ptr[0];
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
DECLARE_XBOXKRNL_EXPORT(XAudioRegisterRenderDriverClient,
                        ExportTag::kImplemented | ExportTag::kAudio);

dword_result_t XAudioUnregisterRenderDriverClient(lpunknown_t driver_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->UnregisterClient(driver_ptr.guest_address() & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XAudioUnregisterRenderDriverClient,
                        ExportTag::kImplemented | ExportTag::kAudio);

dword_result_t XAudioSubmitRenderDriverFrame(lpunknown_t driver_ptr,
                                             lpunknown_t samples_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->SubmitFrame(driver_ptr.guest_address() & 0x0000FFFF,
                            samples_ptr);

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XAudioSubmitRenderDriverFrame,
                        ExportTag::kImplemented | ExportTag::kAudio);

void RegisterAudioExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state) {
  // Additional XMA* methods are in xboxkrnl_audio_xma.cc.
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
