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
#include "xenia/kernel/objects/xenumerator.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL XamGetSystemVersion_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("XamGetSystemVersion()");
  // eh, just picking one. If we go too low we may break new games, but
  // this value seems to be used for conditionally loading symbols and if
  // we pretend to be old we have less to worry with implementing.
  // 0x200A3200
  // 0x20096B00
  SHIM_SET_RETURN_64(0);
}

SHIM_CALL XGetAVPack_shim(PPCContext* ppc_state, KernelState* state) {
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  SHIM_SET_RETURN_64(6);
}

SHIM_CALL XGetGameRegion_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("XGetGameRegion()");

  SHIM_SET_RETURN_64(0xFFFF);
}

SHIM_CALL XGetLanguage_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("XGetLanguage()");

  uint32_t desired_language = X_LANGUAGE_ENGLISH;

  // Switch the language based on game region.
  // TODO(benvanik): pull from xex header.
  uint32_t game_region = XEX_REGION_NTSCU;
  if (game_region & XEX_REGION_NTSCU) {
    desired_language = X_LANGUAGE_ENGLISH;
  } else if (game_region & XEX_REGION_NTSCJ) {
    desired_language = X_LANGUAGE_JAPANESE;
  }
  // Add more overrides?

  SHIM_SET_RETURN_64(desired_language);
}

SHIM_CALL XamVoiceIsActiveProcess_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  XELOGD("XamVoiceIsActiveProcess()");
  // Returning 0 here will short-circuit a bunch of voice stuff.
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XamGetExecutionId_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t info_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamGetExecutionId(%.8X)", info_ptr);

  auto module = state->GetExecutableModule();
  assert_not_null(module);

  SHIM_SET_MEM_32(info_ptr, module->execution_info_ptr());

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XamLoaderSetLaunchData_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t data_ptr = SHIM_GET_ARG_32(0);
  uint32_t data_size = SHIM_GET_ARG_32(1);

  XELOGD("XamLoaderSetLaunchData(%.8X, %d)", data_ptr, data_size);

  // Unknown return value.
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XamLoaderGetLaunchDataSize_shim(PPCContext* ppc_state,
                                          KernelState* state) {
  uint32_t size_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamLoaderGetLaunchDataSize(%.8X)", size_ptr);

  SHIM_SET_MEM_32(size_ptr, 0);

  SHIM_SET_RETURN_32(1);
}

SHIM_CALL XamLoaderGetLaunchData_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t buffer_ptr = SHIM_GET_ARG_32(0);
  uint32_t buffer_size = SHIM_GET_ARG_32(1);

  XELOGD("XamLoaderGetLaunchData(%.8X, %d)", buffer_ptr, buffer_size);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XamLoaderLaunchTitle_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t name_ptr = SHIM_GET_ARG_32(0);
  const char* name = (const char*)SHIM_MEM_ADDR(name_ptr);
  uint32_t unk2 = SHIM_GET_ARG_32(1);

  XELOGD("XamLoaderLaunchTitle(%.8X(%s), %.8X)", name_ptr, name, unk2);
  assert_always();
}

SHIM_CALL XamLoaderTerminateTitle_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  XELOGD("XamLoaderTerminateTitle()");
  assert_always();
}

SHIM_CALL XamAlloc_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk = SHIM_GET_ARG_32(0);
  uint32_t size = SHIM_GET_ARG_32(1);
  uint32_t out_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamAlloc(%d, %d, %.8X)", unk, size, out_ptr);

  assert_true(unk == 0);

  // Allocate from the heap. Not sure why XAM does this specially, perhaps
  // it keeps stuff in a separate heap?
  uint32_t ptr = state->memory()->SystemHeapAlloc(size);
  SHIM_SET_MEM_32(out_ptr, ptr);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamFree_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamFree(%.8X)", ptr);

  state->memory()->SystemHeapFree(ptr);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamEnumerate_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t zero = SHIM_GET_ARG_32(1);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(2);
  uint32_t buffer_length = SHIM_GET_ARG_32(3);
  uint32_t item_count_ptr = SHIM_GET_ARG_32(4);
  uint32_t overlapped_ptr = SHIM_GET_ARG_32(5);

  assert_true(zero == 0);

  XELOGD("XamEnumerate(%.8X, %d, %.8X, %d, %.8X, %.8X)", handle, zero,
         buffer_ptr, buffer_length, item_count_ptr, overlapped_ptr);

  auto e = state->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    if (overlapped_ptr) {
      state->CompleteOverlappedImmediateEx(overlapped_ptr, 0,
                                           X_ERROR_INVALID_HANDLE, 0);
      SHIM_SET_RETURN_64(X_ERROR_IO_PENDING);
    } else {
      SHIM_SET_RETURN_64(X_ERROR_INVALID_HANDLE);
    }
    return;
  }

  auto item_count = e->item_count();
  e->WriteItems(SHIM_MEM_ADDR(buffer_ptr));

  X_RESULT result = item_count ? X_ERROR_SUCCESS : X_ERROR_NO_MORE_FILES;
  if (item_count_ptr) {
    assert_zero(overlapped_ptr);
    SHIM_SET_MEM_32(item_count_ptr, item_count);
  } else if (overlapped_ptr) {
    assert_zero(item_count_ptr);
    state->CompleteOverlappedImmediateEx(overlapped_ptr, result, result,
                                         item_count);
    result = X_ERROR_IO_PENDING;
  } else {
    assert_always();
    result = X_ERROR_INVALID_PARAMETER;
  }

  SHIM_SET_RETURN_64(result);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterInfoExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", XamGetSystemVersion, state);
  SHIM_SET_MAPPING("xam.xex", XGetAVPack, state);
  SHIM_SET_MAPPING("xam.xex", XGetGameRegion, state);
  SHIM_SET_MAPPING("xam.xex", XGetLanguage, state);

  SHIM_SET_MAPPING("xam.xex", XamVoiceIsActiveProcess, state);
  SHIM_SET_MAPPING("xam.xex", XamGetExecutionId, state);

  SHIM_SET_MAPPING("xam.xex", XamLoaderSetLaunchData, state);
  SHIM_SET_MAPPING("xam.xex", XamLoaderGetLaunchDataSize, state);
  SHIM_SET_MAPPING("xam.xex", XamLoaderGetLaunchData, state);
  SHIM_SET_MAPPING("xam.xex", XamLoaderLaunchTitle, state);
  SHIM_SET_MAPPING("xam.xex", XamLoaderTerminateTitle, state);

  SHIM_SET_MAPPING("xam.xex", XamAlloc, state);
  SHIM_SET_MAPPING("xam.xex", XamFree, state);

  SHIM_SET_MAPPING("xam.xex", XamEnumerate, state);
}
