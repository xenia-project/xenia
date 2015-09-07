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
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

constexpr uint32_t X_LANGUAGE_ENGLISH = 1;
constexpr uint32_t X_LANGUAGE_JAPANESE = 2;

SHIM_CALL XamGetSystemVersion_shim(PPCContext* ppc_context,
                                   KernelState* kernel_state) {
  XELOGD("XamGetSystemVersion()");
  // eh, just picking one. If we go too low we may break new games, but
  // this value seems to be used for conditionally loading symbols and if
  // we pretend to be old we have less to worry with implementing.
  // 0x200A3200
  // 0x20096B00
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XGetAVPack_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  SHIM_SET_RETURN_32(6);
}

SHIM_CALL XGetGameRegion_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  XELOGD("XGetGameRegion()");

  SHIM_SET_RETURN_32(0xFFFF);
}

SHIM_CALL XGetLanguage_shim(PPCContext* ppc_context,
                            KernelState* kernel_state) {
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

  SHIM_SET_RETURN_32(desired_language);
}

SHIM_CALL XamGetExecutionId_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t info_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamGetExecutionId(%.8X)", info_ptr);

  auto module = kernel_state->GetExecutableModule();
  assert_not_null(module);

  uint32_t guest_hdr_ptr;
  X_STATUS result =
      module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &guest_hdr_ptr);

  if (XFAILED(result)) {
    SHIM_SET_RETURN_32(result);
    return;
  }

  SHIM_SET_MEM_32(info_ptr, guest_hdr_ptr);
  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

dword_result_t XamLoaderSetLaunchData(lpvoid_t data, dword_t size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  if (loader_data.launch_data_ptr) {
    kernel_memory()->SystemHeapFree(loader_data.launch_data_ptr);
  }

  loader_data.launch_data_ptr = kernel_memory()->SystemHeapAlloc(size);
  loader_data.launch_data_size = size;

  std::memcpy(kernel_memory()->TranslateVirtual(loader_data.launch_data_ptr),
              data, size);

  // FIXME: Unknown return value.
  return 0;
}
DECLARE_XAM_EXPORT(XamLoaderSetLaunchData, ExportTag::kSketchy);

dword_result_t XamLoaderGetLaunchDataSize(lpdword_t size_ptr) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  *size_ptr = xam->loader_data().launch_data_size;

  return 0;
}
DECLARE_XAM_EXPORT(XamLoaderGetLaunchDataSize, ExportTag::kSketchy);

dword_result_t XamLoaderGetLaunchData(lpvoid_t buffer_ptr,
                                      dword_t buffer_size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  if (loader_data.launch_data_ptr) {
    uint8_t* loader_buffer_ptr =
        kernel_memory()->TranslateVirtual(loader_data.launch_data_ptr);

    uint32_t copy_size =
        std::min(loader_data.launch_data_size, (uint32_t)buffer_size);

    std::memcpy(buffer_ptr, loader_buffer_ptr, copy_size);
  }

  return 0;
}
DECLARE_XAM_EXPORT(XamLoaderGetLaunchData, ExportTag::kSketchy);

void XamLoaderLaunchTitle(lpstring_t raw_name, dword_t flags) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  loader_data.launch_flags = flags;

  // Translate the launch path to a full path.
  if (raw_name) {
    std::string name = xe::find_name_from_path(std::string(raw_name));
    std::string path(raw_name);
    if (name == std::string(raw_name)) {
      path = xe::join_paths(
          xe::find_base_path(kernel_state()->GetExecutableModule()->path()),
          name);
    }

    loader_data.launch_path = path;
  } else {
    assert_always("Game requested exit to dashboard via XamLoaderLaunchTitle");
  }

  // This function does not return.
  kernel_state()->TerminateTitle(true);
}
DECLARE_XAM_EXPORT(XamLoaderLaunchTitle, ExportTag::kSketchy);

void XamLoaderTerminateTitle() {
  // This function does not return.
  kernel_state()->TerminateTitle(true);
}
DECLARE_XAM_EXPORT(XamLoaderTerminateTitle, ExportTag::kSketchy);

SHIM_CALL XamAlloc_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t unk = SHIM_GET_ARG_32(0);
  uint32_t size = SHIM_GET_ARG_32(1);
  uint32_t out_ptr = SHIM_GET_ARG_32(2);

  XELOGD("XamAlloc(%d, %d, %.8X)", unk, size, out_ptr);

  assert_true(unk == 0);

  // Allocate from the heap. Not sure why XAM does this specially, perhaps
  // it keeps stuff in a separate heap?
  uint32_t ptr = kernel_state->memory()->SystemHeapAlloc(size);
  SHIM_SET_MEM_32(out_ptr, ptr);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XamFree_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);

  XELOGD("XamFree(%.8X)", ptr);

  kernel_state->memory()->SystemHeapFree(ptr);

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L518
dword_result_t XamEnumerate(dword_t handle, dword_t flags, lpvoid_t buffer,
                            dword_t buffer_length, lpdword_t items_returned,
                            pointer_t<XAM_OVERLAPPED> overlapped) {
  assert_true(flags == 0);

  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped, X_ERROR_INVALID_HANDLE, X_ERROR_INVALID_HANDLE, 0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_INVALID_HANDLE;
    }
  }

  buffer.Zero(buffer_length);
  X_RESULT result =
      e->WriteItem(buffer) ? X_ERROR_SUCCESS : X_ERROR_NO_MORE_FILES;

  // Return X_ERROR_NO_MORE_FILES in HRESULT form.
  X_HRESULT extended_result = result != 0 ? 0x80070012 : 0;
  if (items_returned) {
    assert_true(!overlapped);
    *items_returned = result == X_ERROR_SUCCESS ? 1 : 0;

    return result;
  } else if (overlapped) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped, result, extended_result,
        result == X_ERROR_SUCCESS ? e->item_count() : 0);

    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT(XamEnumerate, ExportTag::kImplemented);

void RegisterInfoExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", XamGetSystemVersion, state);
  SHIM_SET_MAPPING("xam.xex", XGetAVPack, state);
  SHIM_SET_MAPPING("xam.xex", XGetGameRegion, state);
  SHIM_SET_MAPPING("xam.xex", XGetLanguage, state);

  SHIM_SET_MAPPING("xam.xex", XamGetExecutionId, state);

  SHIM_SET_MAPPING("xam.xex", XamAlloc, state);
  SHIM_SET_MAPPING("xam.xex", XamFree, state);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
