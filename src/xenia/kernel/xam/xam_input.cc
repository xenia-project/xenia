/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

using xe::hid::X_INPUT_CAPABILITIES;
using xe::hid::X_INPUT_KEYSTROKE;
using xe::hid::X_INPUT_STATE;
using xe::hid::X_INPUT_VIBRATION;

constexpr uint32_t XINPUT_FLAG_GAMEPAD = 0x01;
constexpr uint32_t XINPUT_FLAG_ANY_USER = 1 << 30;

dword_result_t XamResetInactivity(dword_t unk) {
  XELOGD("XamResetInactivity(%d)", unk);

  // Result ignored.
  return 0;
}
DECLARE_XAM_EXPORT(XamResetInactivity, ExportTag::kImplemented);

dword_result_t XamEnableInactivityProcessing(dword_t zero, dword_t unk) {
  XELOGD("XamEnableInactivityProcessing(%d, %d)", zero, unk);

  // Expects 0.
  return 0;
}
DECLARE_XAM_EXPORT(XamEnableInactivityProcessing, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetcapabilities(v=vs.85).aspx
dword_result_t XamInputGetCapabilities(
    dword_t user_index, dword_t flags,
    pointer_t<X_INPUT_CAPABILITIES> caps_ptr) {
  XELOGD("XamInputGetCapabilities(%d, %.8X, %.8X)", user_index, flags,
         caps_ptr);
  uint32_t usr_idx = user_index;

  if (!caps_ptr) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if ((usr_idx & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    usr_idx = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result = input_system->GetCapabilities(usr_idx, flags, caps_ptr);
  return result;
}
DECLARE_XAM_EXPORT(XamInputGetCapabilities, ExportTag::kImplemented);

dword_result_t XamInputGetCapabilitiesEx(
    dword_t unk, dword_t user_index, dword_t flags,
    pointer_t<X_INPUT_CAPABILITIES> caps_ptr) {
  XELOGD("XamInputGetCapabilitiesEx(%d, %d, %.8X, %.8X)", unk, user_index,
         flags, caps_ptr);
  uint32_t usr_idx = user_index;

  if (!caps_ptr) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if ((usr_idx & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    usr_idx = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result = input_system->GetCapabilities(usr_idx, flags, caps_ptr);
  return result;
}
DECLARE_XAM_EXPORT(XamInputGetCapabilitiesEx, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetstate(v=vs.85).aspx
dword_result_t XamInputGetState(dword_t user_index, dword_t flags,
                                pointer_t<X_INPUT_STATE> inp_state_ptr) {
  XELOGD("XamInputGetState(%d, %.8X, %.8X)", user_index, flags, inp_state_ptr);
  uint32_t usr_idx = user_index;

  // Games call this with a NULL state ptr, probably as a query.

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if ((usr_idx & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    usr_idx = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result = input_system->GetState(usr_idx, inp_state_ptr);
  return result;
}
DECLARE_XAM_EXPORT(XamInputGetState, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputsetstate(v=vs.85).aspx
dword_result_t XamInputSetState(dword_t user_index, dword_t unk,
                                pointer_t<X_INPUT_VIBRATION> vibration_ptr) {
  XELOGD("XamInputSetState(%d, %.8X, %.8X)", user_index, unk, vibration_ptr);
  uint32_t usr_idx = user_index;

  if (!vibration_ptr) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if ((usr_idx & 0xFF) == 0xFF) {
    // Always pin user to 0.
    usr_idx = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result = input_system->SetState(usr_idx, vibration_ptr);
  return result;
}
DECLARE_XAM_EXPORT(XamInputSetState, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetkeystroke(v=vs.85).aspx
dword_result_t XamInputGetKeystroke(
    dword_t user_index, dword_t flags,
    pointer_t<X_INPUT_KEYSTROKE> keystroke_ptr) {
  // http://ffplay360.googlecode.com/svn/Test/Common/AtgXime.cpp
  // user index = index or XUSER_INDEX_ANY
  // flags = XINPUT_FLAG_GAMEPAD (| _ANYUSER | _ANYDEVICE)

  XELOGD("XamInputGetKeystroke(%d, %.8X, %.8X)", user_index, flags,
         keystroke_ptr);
  uint32_t usr_idx = user_index;

  if (!keystroke_ptr) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if ((usr_idx & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    usr_idx = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result = input_system->GetKeystroke(usr_idx, flags, keystroke_ptr);
  return result;
}
DECLARE_XAM_EXPORT(XamInputGetKeystroke, ExportTag::kImplemented);

// Same as non-ex, just takes a pointer to user index.
dword_result_t XamInputGetKeystrokeEx(
    lpdword_t user_index_ptr, dword_t flags,
    pointer_t<X_INPUT_KEYSTROKE> keystroke_ptr) {
  uint32_t user_index = *user_index_ptr;

  XELOGD("XamInputGetKeystrokeEx(%.8X(%d), %.8X, %.8X)", user_index_ptr,
         user_index, flags, keystroke_ptr);

  if (!keystroke_ptr) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();

  X_RESULT result =
      input_system->GetKeystroke(user_index, flags, keystroke_ptr);
  if (XSUCCEEDED(result)) {
    *user_index_ptr = keystroke_ptr->user_index.value;
  }
  return result;
}
DECLARE_XAM_EXPORT(XamInputGetKeystrokeEx, ExportTag::kImplemented);

dword_result_t XamUserGetDeviceContext(dword_t user_index, dword_t unk,
                                       lpdword_t out_ptr) {
  XELOGD("XamUserGetDeviceContext(%d, %d, %.8X)", user_index, unk, out_ptr);

  // Games check the result - usually with some masking.
  // If this function fails they assume zero, so let's fail AND
  // set zero just to be safe.
  *out_ptr = 0;
  if (!user_index || (user_index & 0xFF) == 0xFF) {
    return 0;
  } else {
    return -1;
  }
}
DECLARE_XAM_EXPORT(XamUserGetDeviceContext, ExportTag::kImplemented);

void RegisterInputExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state) {
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
