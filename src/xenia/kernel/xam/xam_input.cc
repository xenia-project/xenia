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

SHIM_CALL XamResetInactivity_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t unk = SHIM_GET_ARG_32(0);

  XELOGD("XamResetInactivity(%d)", unk);

  // Result ignored.
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XamEnableInactivityProcessing_shim(PPCContext* ppc_context,
                                             KernelState* kernel_state) {
  uint32_t zero = SHIM_GET_ARG_32(0);
  uint32_t unk = SHIM_GET_ARG_32(1);

  XELOGD("XamEnableInactivityProcessing(%d, %d)", zero, unk);

  // Expects 0.
  SHIM_SET_RETURN_32(0);
}

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetcapabilities(v=vs.85).aspx
SHIM_CALL XamInputGetCapabilities_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t caps_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamInputGetCapabilities(%d, %.8X, %.8X)", user_index, flags,
         caps_ptr);

  if (!caps_ptr) {
    SHIM_SET_RETURN_32(X_ERROR_BAD_ARGUMENTS);
    return;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto caps = SHIM_STRUCT(X_INPUT_CAPABILITIES, caps_ptr);
  X_RESULT result = input_system->GetCapabilities(user_index, flags, caps);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XamInputGetCapabilitiesEx_shim(PPCContext* ppc_context,
                                         KernelState* kernel_state) {
  uint32_t unk = SHIM_GET_ARG_32(0);
  uint32_t user_index = SHIM_GET_ARG_32(1);
  uint32_t flags = SHIM_GET_ARG_32(2);
  uint32_t caps_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XamInputGetCapabilitiesEx(%d, %d, %.8X, %.8X)", unk, user_index,
         flags, caps_ptr);

  if (!caps_ptr) {
    SHIM_SET_RETURN_32(X_ERROR_BAD_ARGUMENTS);
    return;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto caps = SHIM_STRUCT(X_INPUT_CAPABILITIES, caps_ptr);
  X_RESULT result = input_system->GetCapabilities(user_index, flags, caps);
  SHIM_SET_RETURN_32(result);
}

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetstate(v=vs.85).aspx
SHIM_CALL XamInputGetState_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t state_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamInputGetState(%d, %.8X, %.8X)", user_index, flags, state_ptr);

  // Games call this with a NULL state ptr, probably as a query.

  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto input_state = SHIM_STRUCT(X_INPUT_STATE, state_ptr);
  X_RESULT result = input_system->GetState(user_index, input_state);
  SHIM_SET_RETURN_32(result);
}

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputsetstate(v=vs.85).aspx
SHIM_CALL XamInputSetState_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk = SHIM_GET_ARG_32(1);
  uint32_t vibration_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamInputSetState(%d, %.8X, %.8X)", user_index, unk, vibration_ptr);

  if (!vibration_ptr) {
    SHIM_SET_RETURN_32(X_ERROR_BAD_ARGUMENTS);
    return;
  }
  if ((user_index & 0xFF) == 0xFF) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto vibration = SHIM_STRUCT(X_INPUT_VIBRATION, vibration_ptr);
  X_RESULT result = input_system->SetState(user_index, vibration);
  SHIM_SET_RETURN_32(result);
}

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetkeystroke(v=vs.85).aspx
SHIM_CALL XamInputGetKeystroke_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t keystroke_ptr = SHIM_GET_ARG_32(2);

  // http://ffplay360.googlecode.com/svn/Test/Common/AtgXime.cpp
  // user index = index or XUSER_INDEX_ANY
  // flags = XINPUT_FLAG_GAMEPAD (| _ANYUSER | _ANYDEVICE)

  XELOGD("XamInputGetKeystroke(%d, %.8X, %.8X)", user_index, flags,
         keystroke_ptr);

  if (!keystroke_ptr) {
    SHIM_SET_RETURN_32(X_ERROR_BAD_ARGUMENTS);
    return;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto keystroke = SHIM_STRUCT(X_INPUT_KEYSTROKE, keystroke_ptr);
  X_RESULT result = input_system->GetKeystroke(user_index, flags, keystroke);
  SHIM_SET_RETURN_32(result);
}

// Same as non-ex, just takes a pointer to user index.
SHIM_CALL XamInputGetKeystrokeEx_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t user_index_ptr = SHIM_GET_ARG_32(0);
  uint32_t flags = SHIM_GET_ARG_32(1);
  uint32_t keystroke_ptr = SHIM_GET_ARG_32(2);

  uint32_t user_index = SHIM_MEM_32(user_index_ptr);

  XELOGD("XamInputGetKeystrokeEx(%.8X(%d), %.8X, %.8X)", user_index_ptr,
         user_index, flags, keystroke_ptr);

  if (!keystroke_ptr) {
    SHIM_SET_RETURN_32(X_ERROR_BAD_ARGUMENTS);
    return;
  }
  if ((flags & 0xFF) && (flags & XINPUT_FLAG_GAMEPAD) == 0) {
    // Ignore any query for other types of devices.
    SHIM_SET_RETURN_32(X_ERROR_DEVICE_NOT_CONNECTED);
    return;
  }
  if ((user_index & 0xFF) == 0xFF || (flags & XINPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    user_index = 0;
  }

  auto input_system = kernel_state->emulator()->input_system();

  auto keystroke = SHIM_STRUCT(X_INPUT_KEYSTROKE, keystroke_ptr);
  X_RESULT result = input_system->GetKeystroke(user_index, flags, keystroke);
  if (XSUCCEEDED(result)) {
    SHIM_SET_MEM_32(user_index_ptr, keystroke->user_index);
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XamUserGetDeviceContext_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t user_index = SHIM_GET_ARG_32(0);
  uint32_t unk = SHIM_GET_ARG_32(1);
  uint32_t out_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamUserGetDeviceContext(%d, %d, %.8X)", user_index, unk, out_ptr);

  // Games check the result - usually with some masking.
  // If this function fails they assume zero, so let's fail AND
  // set zero just to be safe.
  SHIM_SET_MEM_32(out_ptr, 0);
  if (!user_index || (user_index & 0xFF) == 0xFF) {
    SHIM_SET_RETURN_32(0);
  } else {
    SHIM_SET_RETURN_32(-1);
  }
}

void RegisterInputExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamResetInactivity, state);
  SHIM_SET_MAPPING("xam.xex", XamEnableInactivityProcessing, state);
  SHIM_SET_MAPPING("xam.xex", XamInputGetCapabilities, state);
  SHIM_SET_MAPPING("xam.xex", XamInputGetCapabilitiesEx, state);
  SHIM_SET_MAPPING("xam.xex", XamInputGetState, state);
  SHIM_SET_MAPPING("xam.xex", XamInputSetState, state);
  SHIM_SET_MAPPING("xam.xex", XamInputGetKeystroke, state);
  SHIM_SET_MAPPING("xam.xex", XamInputGetKeystrokeEx, state);
  SHIM_SET_MAPPING("xam.xex", XamUserGetDeviceContext, state);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
