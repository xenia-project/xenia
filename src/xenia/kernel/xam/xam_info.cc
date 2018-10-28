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

dword_result_t XamGetSystemVersion() {
  // eh, just picking one. If we go too low we may break new games, but
  // this value seems to be used for conditionally loading symbols and if
  // we pretend to be old we have less to worry with implementing.
  // 0x200A3200
  // 0x20096B00
  return 0;
}
DECLARE_XAM_EXPORT(XamGetSystemVersion, ExportTag::kStub);

void XCustomRegisterDynamicActions() {
  // ???
}
DECLARE_XAM_EXPORT(XCustomRegisterDynamicActions, ExportTag::kStub);

dword_result_t XGetAVPack() {
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  return 6;
}
DECLARE_XAM_EXPORT(XGetAVPack, ExportTag::kStub);

dword_result_t XGetGameRegion() { return 0xFFFF; }
DECLARE_XAM_EXPORT(XGetGameRegion, ExportTag::kStub);

dword_result_t XGetLanguage() {
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

  return desired_language;
}
DECLARE_XAM_EXPORT(XGetLanguage, ExportTag::kImplemented);

dword_result_t XamGetExecutionId(lpdword_t info_ptr) {
  auto module = kernel_state()->GetExecutableModule();
  assert_not_null(module);

  uint32_t guest_hdr_ptr;
  X_STATUS result =
      module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &guest_hdr_ptr);

  if (XFAILED(result)) {
    return result;
  }

  *info_ptr = guest_hdr_ptr;
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT(XamGetExecutionId, ExportTag::kImplemented);

dword_result_t XamLoaderSetLaunchData(lpvoid_t data, dword_t size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  loader_data.launch_data_present = size ? true : false;
  loader_data.launch_data.resize(size);
  std::memcpy(loader_data.launch_data.data(), data, size);

  // FIXME: Unknown return value.
  return 0;
}
DECLARE_XAM_EXPORT(XamLoaderSetLaunchData, ExportTag::kSketchy);

dword_result_t XamLoaderGetLaunchDataSize(lpdword_t size_ptr) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();

  if (!size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (loader_data.launch_data_present) {
    *size_ptr = uint32_t(xam->loader_data().launch_data.size());
    return X_ERROR_SUCCESS;
  }

  *size_ptr = 0;
  return X_ERROR_NOT_FOUND;
}
DECLARE_XAM_EXPORT(XamLoaderGetLaunchDataSize, ExportTag::kSketchy);

dword_result_t XamLoaderGetLaunchData(lpvoid_t buffer_ptr,
                                      dword_t buffer_size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  if (!loader_data.launch_data_present) {
    return X_ERROR_NOT_FOUND;
  }

  uint32_t copy_size =
      std::min(uint32_t(loader_data.launch_data.size()), uint32_t(buffer_size));
  std::memcpy(buffer_ptr, loader_data.launch_data.data(), copy_size);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamLoaderGetLaunchData, ExportTag::kSketchy);

void XamLoaderLaunchTitle(lpstring_t raw_name, dword_t flags) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  loader_data.launch_flags = flags;

  // Translate the launch path to a full path.
  if (raw_name && raw_name.value() == "") {
    loader_data.launch_path = "game:\\default.xex";
  } else if (raw_name) {
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
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT(XamLoaderLaunchTitle, ExportTag::kSketchy);

void XamLoaderTerminateTitle() {
  // This function does not return.
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT(XamLoaderTerminateTitle, ExportTag::kSketchy);

dword_result_t XamAlloc(dword_t unk, dword_t size, lpdword_t out_ptr) {
  assert_true(unk == 0);

  // Allocate from the heap. Not sure why XAM does this specially, perhaps
  // it keeps stuff in a separate heap?
  uint32_t ptr = kernel_state()->memory()->SystemHeapAlloc(size);
  *out_ptr = ptr;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamAlloc, ExportTag::kImplemented);

dword_result_t XamFree(lpdword_t ptr) {
  kernel_state()->memory()->SystemHeapFree(ptr.guest_address());

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamFree, ExportTag::kImplemented);

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

  X_RESULT result;
  uint32_t item_count = 0;

  if (buffer_length < e->item_size()) {
    result = X_ERROR_INSUFFICIENT_BUFFER;
  } else if (e->current_item() >= e->item_count()) {
    result = X_ERROR_NO_MORE_FILES;
  } else {
    auto item_buffer = buffer.as<uint8_t*>();
    auto max_items = buffer_length / e->item_size();
    while (max_items--) {
      if (!e->WriteItem(item_buffer)) {
        break;
      }
      item_buffer += e->item_size();
      item_count++;
    }
    result = X_ERROR_SUCCESS;
  }

  // Return X_ERROR_NO_MORE_FILES in HRESULT form.
  X_HRESULT extended_result = result != 0 ? X_HRESULT_FROM_WIN32(result) : 0;
  if (items_returned) {
    assert_true(!overlapped);
    *items_returned = result == X_ERROR_SUCCESS ? item_count : 0;
    return result;
  } else if (overlapped) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped, result, extended_result,
        result == X_ERROR_SUCCESS ? item_count : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT(XamEnumerate, ExportTag::kImplemented);

dword_result_t XamCreateEnumeratorHandle(unknown_t unk1, unknown_t unk2,
                                         unknown_t unk3, unknown_t unk4,
                                         unknown_t unk5, unknown_t unk6,
                                         unknown_t unk7, unknown_t unk8) {
  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT(XamCreateEnumeratorHandle, ExportTag::kStub);

dword_result_t XamGetPrivateEnumStructureFromHandle(unknown_t unk1,
                                                    unknown_t unk2) {
  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT(XamGetPrivateEnumStructureFromHandle, ExportTag::kStub);

void RegisterInfoExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe