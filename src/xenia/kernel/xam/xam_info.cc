/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_error.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_modules.h>
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_memory.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/fmt/include/fmt/xchar.h"

DEFINE_int32(avpack, 8,
             "Video modes\n"
             " 0 = PAL-60 Component (SD)\n"
             " 1 = Unused\n"
             " 2 = PAL-60 SCART\n"
             " 3 = 480p Component (HD)\n"
             " 4 = HDMI+A\n"
             " 5 = PAL-60 Composite/S-Video\n"
             " 6 = VGA\n"
             " 7 = TV PAL-60\n"
             " 8 = HDMI (default)",
             "Video");
DECLARE_int32(user_country);
DECLARE_int32(user_language);
DECLARE_uint32(audio_flag);

DEFINE_bool(staging_mode, 0,
            "Enables preview mode in dashboards to render debug information.",
            "Kernel");

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/tpn/winsdk-10/blob/master/Include/10.0.14393.0/km/wdm.h#L15539
typedef enum _MODE { KernelMode, UserMode, MaximumMode } MODE;

dword_result_t XamFeatureEnabled_entry(dword_t unk) { return 0; }
DECLARE_XAM_EXPORT1(XamFeatureEnabled, kNone, kStub);

dword_result_t XamGetStagingMode_entry() { return cvars::staging_mode; }
DECLARE_XAM_EXPORT1(XamGetStagingMode, kNone, kStub);

// Empty stub schema binary.
uint8_t schema_bin[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00,
    0x00, 0x2C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
};

dword_result_t XamGetOnlineSchema_entry() {
  static uint32_t schema_guest = 0;

  if (!schema_guest) {
    schema_guest =
        kernel_state()->memory()->SystemHeapAlloc(8 + sizeof(schema_bin));
    auto schema = kernel_state()->memory()->TranslateVirtual(schema_guest);
    std::memcpy(schema + 8, schema_bin, sizeof(schema_bin));
    xe::store_and_swap<uint32_t>(schema + 0, schema_guest + 8);
    xe::store_and_swap<uint32_t>(schema + 4, sizeof(schema_bin));
  }

  // return pointer to the schema ptr/schema size struct
  return schema_guest;
}
DECLARE_XAM_EXPORT1(XamGetOnlineSchema, kNone, kImplemented);

void XamFormatDateString_entry(dword_t locale_format, qword_t filetime,
                               lpvoid_t output_buffer, dword_t output_count) {
  output_buffer.Zero(output_count * sizeof(char16_t));

  auto tp = xe::chrono::WinSystemClock::to_sys(
      xe::chrono::WinSystemClock::from_file_time(filetime));
  auto dp = date::floor<date::days>(tp);
  auto year_month_day = date::year_month_day{dp};

  auto str = fmt::format(u"{:02d}/{:02d}/{}",
                         static_cast<unsigned>(year_month_day.month()),
                         static_cast<unsigned>(year_month_day.day()),
                         static_cast<int>(year_month_day.year()));
  xe::string_util::copy_and_swap_truncating(output_buffer.as<char16_t*>(), str,
                                            output_count);
}
DECLARE_XAM_EXPORT1(XamFormatDateString, kNone, kImplemented);

void XamFormatTimeString_entry(dword_t unk, qword_t filetime,
                               lpvoid_t output_buffer, dword_t output_count) {
  output_buffer.Zero(output_count * sizeof(char16_t));

  auto tp = xe::chrono::WinSystemClock::to_sys(
      xe::chrono::WinSystemClock::from_file_time(filetime));
  auto dp = date::floor<date::days>(tp);
  auto time = date::hh_mm_ss{date::floor<std::chrono::milliseconds>(tp - dp)};

  auto str = fmt::format(u"{:02d}:{:02d}", time.hours().count(),
                         time.minutes().count());
  xe::string_util::copy_and_swap_truncating(output_buffer.as<char16_t*>(), str,
                                            output_count);
}
DECLARE_XAM_EXPORT1(XamFormatTimeString, kNone, kImplemented);

dword_result_t keXamBuildResourceLocator(uint64_t module,
                                         const std::u16string& container,
                                         const std::u16string& resource,
                                         lpvoid_t buffer_ptr,
                                         uint32_t buffer_count) {
  std::u16string path;
  if (!module) {
    path = fmt::format(u"file://media:/{}.xzp#{}", container, resource);
    XELOGD(
        "XamBuildResourceLocator({0}) returning locator to local file {0}.xzp",
        xe::to_utf8(container));
  } else {
    path = fmt::format(u"section://{:X},{}#{}", (uint32_t)module, container,
                       resource);
  }
  xe::string_util::copy_and_swap_truncating(buffer_ptr.as<char16_t*>(), path,
                                            buffer_count);
  return 0;
}

dword_result_t XamBuildResourceLocator_entry(qword_t module,
                                             lpu16string_t container,
                                             lpu16string_t resource,
                                             lpvoid_t buffer_ptr,
                                             dword_t buffer_count) {
  return keXamBuildResourceLocator(module, container.value(), resource.value(),
                                   buffer_ptr, buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildResourceLocator, kNone, kImplemented);

dword_result_t XamBuildGamercardResourceLocator_entry(lpu16string_t filename,
                                                      lpvoid_t buffer_ptr,
                                                      dword_t buffer_count) {
  // On an actual xbox these funcs would return a locator to xam.xex resources,
  // but for Xenia we can return a locator to the resources as local files. (big
  // thanks to MS for letting XamBuildResourceLocator return local file
  // locators!)

  // If you're running an app that'll need them, make sure to extract xam.xex
  // resources with xextool ("xextool -d . xam.xex") and add a .xzp extension.

  return keXamBuildResourceLocator(0, u"gamercrd", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildGamercardResourceLocator, kNone, kImplemented);

dword_result_t XamBuildSharedSystemResourceLocator_entry(lpu16string_t filename,
                                                         lpvoid_t buffer_ptr,
                                                         dword_t buffer_count) {
  // see notes inside XamBuildGamercardResourceLocator above
  return keXamBuildResourceLocator(0, u"shrdres", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildSharedSystemResourceLocator, kNone, kImplemented);

dword_result_t XamBuildLegacySystemResourceLocator_entry(lpu16string_t filename,
                                                         lpvoid_t buffer_ptr,
                                                         dword_t buffer_count) {
  return XamBuildSharedSystemResourceLocator_entry(filename, buffer_ptr,
                                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildLegacySystemResourceLocator, kNone, kImplemented);

dword_result_t XamBuildXamResourceLocator_entry(lpu16string_t filename,
                                                lpvoid_t buffer_ptr,
                                                dword_t buffer_count) {
  return keXamBuildResourceLocator(0, u"xam", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildXamResourceLocator, kNone, kImplemented);

dword_result_t XamGetSystemVersion_entry() {
  // eh, just picking one. If we go too low we may break new games, but
  // this value seems to be used for conditionally loading symbols and if
  // we pretend to be old we have less to worry with implementing.
  // 0x200A3200
  // 0x20096B00
  return 0;
}
DECLARE_XAM_EXPORT1(XamGetSystemVersion, kNone, kStub);

void XCustomRegisterDynamicActions_entry() {
  // ???
}
DECLARE_XAM_EXPORT1(XCustomRegisterDynamicActions, kNone, kStub);

dword_result_t XGetAVPack_entry() {
  // Value from
  // https://github.com/Free60Project/libxenon/blob/920146f/libxenon/drivers/xenos/xenos_videomodes.h
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  return (cvars::avpack);
}
DECLARE_XAM_EXPORT1(XGetAVPack, kNone, kStub);

uint32_t xeXGetGameRegion() {
  static uint32_t const table[] = {
      0xFFFFu, 0x03FFu, 0x02FEu, 0x02FEu, 0x03FFu, 0x02FEu, 0x0201u, 0x03FFu,
      0x02FEu, 0x02FEu, 0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu, 0x02FEu, 0x03FFu,
      0x00FFu, 0xFFFFu, 0x02FEu, 0x03FFu, 0x0102u, 0x03FFu, 0x03FFu, 0x02FEu,
      0x02FEu, 0x02FEu, 0x03FFu, 0x03FFu, 0x03FFu, 0x02FEu, 0x03FFu, 0x02FEu,
      0x02FEu, 0x02FEu, 0x02FEu, 0x02FEu, 0x02FEu, 0x02FEu, 0x03FFu, 0x03FFu,
      0x03FFu, 0x02FEu, 0x02FEu, 0x03FFu, 0x02FEu, 0x02FEu, 0x03FFu, 0x03FFu,
      0x03FFu, 0x02FEu, 0x02FEu, 0x03FFu, 0x03FFu, 0x0101u, 0x03FFu, 0x03FFu,
      0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu, 0x02FEu, 0x02FEu, 0x02FEu, 0x02FEu,
      0x03FFu, 0x03FFu, 0x02FEu, 0x02FEu, 0x03FFu, 0x0102u, 0x03FFu, 0x00FFu,
      0x03FFu, 0x03FFu, 0x02FEu, 0x02FEu, 0x0201u, 0x03FFu, 0x03FFu, 0x03FFu,
      0x03FFu, 0x03FFu, 0x02FEu, 0x03FFu, 0x02FEu, 0x03FFu, 0x03FFu, 0x02FEu,
      0x02FEu, 0x03FFu, 0x02FEu, 0x03FFu, 0x02FEu, 0x02FEu, 0xFFFFu, 0x03FFu,
      0x03FFu, 0x03FFu, 0x03FFu, 0x02FEu, 0x03FFu, 0x03FFu, 0x02FEu, 0x00FFu,
      0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu, 0x03FFu};
  auto country = static_cast<uint8_t>(cvars::user_country);
  return country < xe::countof(table) ? table[country] : 0xFFFFu;
}

dword_result_t XGetGameRegion_entry() { return xeXGetGameRegion(); }
DECLARE_XAM_EXPORT1(XGetGameRegion, kNone, kStub);

XLanguage xeGetLanguage(bool extended_languages_support) {
  auto desired_language = static_cast<XLanguage>(cvars::user_language);
  uint32_t region = xeXGetGameRegion();
  auto max_languages = extended_languages_support ? XLanguage::kMaxLanguages
                                                  : XLanguage::kSChinese;
  if (desired_language < max_languages) {
    return desired_language;
  }
  if ((region & 0xff00) != 0x100) {
    return XLanguage::kEnglish;
  }
  switch (region) {
    case 0x101:  // NTSC-J (Japan)
      return XLanguage::kJapanese;
    case 0x102:  // NTSC-J (China)
      return extended_languages_support ? XLanguage::kSChinese
                                        : XLanguage::kEnglish;
    default:
      return XLanguage::kKorean;
  }
}

dword_result_t XGetLanguage_entry() {
  return static_cast<uint32_t>(xeGetLanguage(false));
}
DECLARE_XAM_EXPORT1(XGetLanguage, kNone, kImplemented);

dword_result_t XamGetLanguage_entry() {
  return static_cast<uint32_t>(xeGetLanguage(true));
}
DECLARE_XAM_EXPORT1(XamGetLanguage, kNone, kImplemented);

dword_result_t XamGetCurrentTitleId_entry() {
  return kernel_state()->emulator()->title_id();
}
DECLARE_XAM_EXPORT1(XamGetCurrentTitleId, kNone, kImplemented);

dword_result_t XamIsCurrentTitleDash_entry(const ppc_context_t& ctx) {
  return ctx->kernel_state->title_id() == 0xFFFE07D1;
}
DECLARE_XAM_EXPORT1(XamIsCurrentTitleDash, kNone, kImplemented);

dword_result_t XamGetExecutionId_entry(lpdword_t info_ptr) {
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
DECLARE_XAM_EXPORT1(XamGetExecutionId, kNone, kImplemented);

dword_result_t XamLoaderSetLaunchData_entry(lpvoid_t data, dword_t size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  loader_data.launch_data_present = size ? true : false;
  loader_data.launch_data.resize(size);
  std::memcpy(loader_data.launch_data.data(), data, size);
  return 0;
}
DECLARE_XAM_EXPORT1(XamLoaderSetLaunchData, kNone, kSketchy);

dword_result_t XamLoaderGetLaunchDataSize_entry(lpdword_t size_ptr) {
  if (!size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  if (!loader_data.launch_data_present) {
    *size_ptr = 0;
    return X_ERROR_NOT_FOUND;
  }

  *size_ptr = uint32_t(xam->loader_data().launch_data.size());
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamLoaderGetLaunchDataSize, kNone, kSketchy);

dword_result_t XamLoaderGetLaunchData_entry(lpvoid_t buffer_ptr,
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
DECLARE_XAM_EXPORT1(XamLoaderGetLaunchData, kNone, kSketchy);

void XamLoaderLaunchTitle_entry(lpstring_t raw_name_ptr, dword_t flags) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  loader_data.launch_flags = flags;

  // Translate the launch path to a full path.
  if (raw_name_ptr) {
    auto path = raw_name_ptr.value();
    if (path.empty()) {
      loader_data.launch_path = "game:\\default.xex";
    } else {
      loader_data.launch_path = xe::path_to_utf8(path);
      loader_data.launch_data_present = true;
    }

    xam->SaveLoaderData();

    if (loader_data.launch_data_present) {
      auto display_window = kernel_state()->emulator()->display_window();
      auto imgui_drawer = kernel_state()->emulator()->imgui_drawer();

      if (display_window && imgui_drawer) {
        display_window->app_context().CallInUIThreadSynchronous(
            [imgui_drawer]() {
              xe::ui::ImGuiDialog::ShowMessageBox(
                  imgui_drawer, "Title was restarted",
                  "Title closed with new launch data. \nPlease restart Xenia. "
                  "Game will be loaded automatically.");
            });
      }
    }
  } else {
    assert_always("Game requested exit to dashboard via XamLoaderLaunchTitle");
  }

  // This function does not return.
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT1(XamLoaderLaunchTitle, kNone, kSketchy);

void XamLoaderTerminateTitle_entry() {
  // This function does not return.
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT1(XamLoaderTerminateTitle, kNone, kSketchy);

uint32_t XamAllocImpl(uint32_t flags, uint32_t size,
                      xe::be<uint32_t>* out_ptr) {
  if (flags & 0x00100000) {  // HEAP_ZERO_memory used unless this flag
    // do nothing!
    // maybe we ought to fill it with nonzero garbage, but otherwise this is a
    // flag we can safely ignore
  }

  // Allocate from the heap. Not sure why XAM does this specially, perhaps
  // it keeps stuff in a separate heap?
  // chrispy: there is a set of different heaps it uses, an array of them. the
  // top 4 bits of the 32 bit flags seems to select the heap
  uint32_t ptr = kernel_state()->memory()->SystemHeapAlloc(size);
  *out_ptr = ptr;

  return X_ERROR_SUCCESS;
}

dword_result_t XamAlloc_entry(dword_t flags, dword_t size, lpdword_t out_ptr) {
  return XamAllocImpl(flags, size, out_ptr);
}
DECLARE_XAM_EXPORT1(XamAlloc, kMemory, kImplemented);

static const unsigned short XamPhysicalProtTable[4] = {
    X_PAGE_READONLY, X_PAGE_READWRITE | X_PAGE_NOCACHE, X_PAGE_READWRITE,
    X_PAGE_WRITECOMBINE | X_PAGE_READWRITE};

dword_result_t XamAllocEx_entry(dword_t phys_flags, dword_t flags, dword_t size,
                                lpdword_t out_ptr, const ppc_context_t& ctx) {
  if ((flags & 0x40000000) == 0) {
    return XamAllocImpl(flags, size, out_ptr);
  }

  uint32_t flags_remapped = phys_flags;
  if ((phys_flags & 0xF000000) == 0) {
    // setting default alignment
    flags_remapped = 0xC000000 | phys_flags & 0xF0FFFFFF;
  }

  uint32_t result = xboxkrnl::xeMmAllocatePhysicalMemoryEx(
      2, size, XamPhysicalProtTable[(flags_remapped >> 28) & 0b11], 0,
      0xFFFFFFFF, 1 << ((flags_remapped >> 24) & 0xF));

  if (result && (flags_remapped & 0x40000000) != 0) {
    memset(ctx->TranslateVirtual<uint8_t*>(result), 0, size);
  }

  *out_ptr = result;
  return result ? 0 : 0x8007000E;
}
DECLARE_XAM_EXPORT1(XamAllocEx, kMemory, kImplemented);

dword_result_t XamFree_entry(lpdword_t ptr) {
  kernel_state()->memory()->SystemHeapFree(ptr.guest_address());

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamFree, kMemory, kImplemented);

dword_result_t XamQueryLiveHiveW_entry(lpu16string_t name, lpvoid_t out_buf,
                                       dword_t out_size,
                                       dword_t type /* guess */) {
  return X_STATUS_INVALID_PARAMETER_1;
}
DECLARE_XAM_EXPORT1(XamQueryLiveHiveW, kNone, kStub);

// http://www.noxa.org/blog/2011/02/28/building-an-xbox-360-emulator-part-3-feasibilityos/
// http://www.noxa.org/blog/2011/08/13/building-an-xbox-360-emulator-part-5-xex-files/
dword_result_t RtlSleep_entry(dword_t dwMilliseconds, dword_t bAlertable) {
  uint64_t delay{};

  // Convert the delay time to 100-nanosecond intervals
  delay = dwMilliseconds == -1 ? LLONG_MAX
                               : static_cast<int64_t>(-10000) * dwMilliseconds;

  X_STATUS result = xboxkrnl::KeDelayExecutionThread(MODE::UserMode, bAlertable,
                                                     &delay, nullptr);

  // If the delay was interrupted by an APC, keep delaying the thread
  while (bAlertable && result == X_STATUS_ALERTED) {
    result = xboxkrnl::KeDelayExecutionThread(MODE::UserMode, bAlertable,
                                              &delay, nullptr);
  }

  return result == X_STATUS_SUCCESS ? X_STATUS_SUCCESS : X_STATUS_USER_APC;
}
DECLARE_XAM_EXPORT1(RtlSleep, kNone, kImplemented);

dword_result_t SleepEx_entry(dword_t dwMilliseconds, dword_t bAlertable) {
  return RtlSleep_entry(dwMilliseconds, bAlertable);
}
DECLARE_XAM_EXPORT1(SleepEx, kNone, kImplemented);

// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep
void Sleep_entry(dword_t dwMilliseconds) {
  RtlSleep_entry(dwMilliseconds, false);
}
DECLARE_XAM_EXPORT1(Sleep, kNone, kImplemented);

// https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount
dword_result_t GetTickCount_entry() { return Clock::QueryGuestUptimeMillis(); }
DECLARE_XAM_EXPORT1(GetTickCount, kNone, kImplemented);

dword_result_t RtlSetLastNTError_entry(dword_t error_code) {
  const uint32_t result =
      xe::kernel::xboxkrnl::xeRtlNtStatusToDosError(error_code);
  XThread::SetLastError(result);

  return result;
}
DECLARE_XAM_EXPORT1(RtlSetLastNTError, kNone, kImplemented);

dword_result_t RtlGetLastError_entry() { return XThread::GetLastError(); }
DECLARE_XAM_EXPORT1(RtlGetLastError, kNone, kImplemented);

dword_result_t GetLastError_entry() { return RtlGetLastError_entry(); }
DECLARE_XAM_EXPORT1(GetLastError, kNone, kImplemented);

dword_result_t GetModuleHandleA_entry(lpstring_t module_name) {
  xe::be<uint32_t> module_ptr = 0;
  const X_STATUS error_code = xe::kernel::xboxkrnl::XexGetModuleHandle(
      module_name.value(), &module_ptr);

  if (XFAILED(error_code)) {
    RtlSetLastNTError_entry(error_code);

    return NULL;
  }

  return (uint32_t)module_ptr;
}
DECLARE_XAM_EXPORT1(GetModuleHandleA, kNone, kImplemented);

dword_result_t XapipCreateThread_entry(lpdword_t lpThreadAttributes,
                                       dword_t dwStackSize,
                                       lpvoid_t lpStartAddress,
                                       lpvoid_t lpParameter,
                                       dword_t dwCreationFlags, dword_t unkn,
                                       lpdword_t lpThreadId) {
  uint32_t flags = (dwCreationFlags >> 2) & 1;

  if (unkn != -1) {
    flags |= 1 << unkn << 24;
  }

  xe::be<uint32_t> result = 0;

  const X_STATUS error_code = xe::kernel::xboxkrnl::ExCreateThread(
      &result, dwStackSize, lpThreadId, lpStartAddress, lpParameter, 0, flags);

  if (XFAILED(error_code)) {
    RtlSetLastNTError_entry(error_code);

    return NULL;
  }

  return (uint32_t)result;
}
DECLARE_XAM_EXPORT1(XapipCreateThread, kNone, kImplemented);

dword_result_t CreateThread_entry(lpdword_t lpThreadAttributes,
                                  dword_t dwStackSize, lpvoid_t lpStartAddress,
                                  lpvoid_t lpParameter, dword_t dwCreationFlags,
                                  lpdword_t lpThreadId) {
  return XapipCreateThread_entry(lpThreadAttributes, dwStackSize,
                                 lpStartAddress, lpParameter, dwCreationFlags,
                                 -1, lpThreadId);
}
DECLARE_XAM_EXPORT1(CreateThread, kNone, kImplemented);

dword_result_t CloseHandle_entry(dword_t hObject) {
  const X_STATUS error_code = xe::kernel::xboxkrnl::NtClose(hObject);

  if (XFAILED(error_code)) {
    RtlSetLastNTError_entry(error_code);

    return false;
  }

  return true;
}
DECLARE_XAM_EXPORT1(CloseHandle, kNone, kImplemented);

dword_result_t ResumeThread_entry(dword_t hThread) {
  uint32_t suspend_count;
  const X_STATUS error_code =
      xe::kernel::xboxkrnl::NtResumeThread(hThread, &suspend_count);

  if (XFAILED(error_code)) {
    RtlSetLastNTError_entry(error_code);

    return -1;
  }

  return suspend_count;
}
DECLARE_XAM_EXPORT1(ResumeThread, kNone, kImplemented);

void ExitThread_entry(dword_t exit_code) {
  xe::kernel::xboxkrnl::ExTerminateThread(exit_code);
}
DECLARE_XAM_EXPORT1(ExitThread, kNone, kImplemented);

dword_result_t GetCurrentThreadId_entry() {
  return XThread::GetCurrentThread()->GetCurrentThreadId();
}
DECLARE_XAM_EXPORT1(GetCurrentThreadId, kNone, kImplemented);

qword_result_t XapiFormatTimeOut_entry(lpqword_t result,
                                       dword_t dwMilliseconds) {
  if (dwMilliseconds == -1) {
    return 0;
  }

  *result = static_cast<int64_t>(-10000) * dwMilliseconds;
  return result.host_address();
}
DECLARE_XAM_EXPORT1(XapiFormatTimeOut, kNone, kImplemented);

dword_result_t WaitForSingleObjectEx_entry(dword_t hHandle,
                                           dword_t dwMilliseconds,
                                           dword_t bAlertable) {
  // TODO(Gliniak): Figure it out to be less janky.
  uint64_t timeout;
  uint64_t* timeout_ptr = reinterpret_cast<uint64_t*>(
      static_cast<uint64_t>(XapiFormatTimeOut_entry(&timeout, dwMilliseconds)));

  X_STATUS result =
      xboxkrnl::NtWaitForSingleObjectEx(hHandle, 1, bAlertable, timeout_ptr);

  while (bAlertable && result == X_STATUS_ALERTED) {
    result =
        xboxkrnl::NtWaitForSingleObjectEx(hHandle, 1, bAlertable, timeout_ptr);
  }

  RtlSetLastNTError_entry(result);
  result = -1;

  return result;
}
DECLARE_XAM_EXPORT1(WaitForSingleObjectEx, kNone, kImplemented);

dword_result_t WaitForSingleObject_entry(dword_t hHandle,
                                         dword_t dwMilliseconds) {
  return WaitForSingleObjectEx_entry(hHandle, dwMilliseconds, 0);
}
DECLARE_XAM_EXPORT1(WaitForSingleObject, kNone, kImplemented);

dword_result_t lstrlenW_entry(lpu16string_t string) {
  // wcslen?
  if (string) {
    return (uint32_t)string.value().length();
  }

  return NULL;
}
DECLARE_XAM_EXPORT1(lstrlenW, kNone, kImplemented);

dword_result_t XGetAudioFlags_entry() { return cvars::audio_flag; }
DECLARE_XAM_EXPORT1(XGetAudioFlags, kNone, kStub);

/*
        todo: this table should instead be pointed to by a member of kernel
   state and initialized along with the process
*/
static int32_t XamRtlRandomTable[128] = {
    1284227242, 1275210071, 573735546,  790525478,  2139871995, 1547161642,
    179605362,  789336058,  688789844,  1801674531, 1563985344, 1957994488,
    1364589140, 1645522239, 287218729,  606747145,  1972579041, 1085031214,
    1425521274, 1482476501, 1844823847, 57989841,   1897051121, 1935655697,
    1078145449, 1960408961, 1682526488, 842925246,  1500820517, 1214440339,
    1647877149, 682003330,  261478967,  2052111302, 162531612,  583907252,
    1336601894, 1715567821, 413027322,  484763869,  1383384898, 1004336348,
    764733703,  854245398,  651377827,  1614895754, 838170752,  1757981266,
    602609370,  1644491937, 926492609,  220523388,  115176313,  725345543,
    261903793,  746137067,  920026266,  1123561945, 1580818891, 1708537768,
    616249376,  1292428093, 562591055,  343818398,  1788223648, 1659004503,
    2077806209, 299502267,  1604221776, 602162358,  630328440,  1606980848,
    1580436667, 1078081533, 492894223,  839522115,  1979792683, 117609710,
    1767777339, 1454471165, 1965331169, 1844237615, 308236825,  329068152,
    412668190,  796845957,  1303643608, 436374069,  1677128483, 527237240,
    813497703,  1060284298, 1770027372, 1177238915, 884357618,  1409082233,
    1958367476, 448539723,  1592454029, 861567501,  963894560,  73586283,
    362288127,  507921405,  113007714,  823518204,  152049171,  1202660629,
    1326574676, 2025429265, 1035525444, 515967899,  1532298954, 2000478354,
    1450960922, 1417001333, 2049760794, 1229272821, 879983540,  1993962763,
    706699826,  776561741,  2111687655, 1343024091, 1637723038, 1220945662,
    484061587,  1390067357};

/*
        Follows xam exactly, the updates to the random table are probably racy.
*/
dword_result_t RtlRandom_entry(lpdword_t seed_out) {
  int32_t table_seed_new = (0x7FFFFFED * *seed_out + 0x7FFFFFC3) % 0x7FFFFFFF;
  *seed_out = table_seed_new;
  uint32_t param_seed_new =
      (0x7FFFFFED * table_seed_new + 0x7FFFFFC3) % 0x7FFFFFFFu;
  *seed_out = param_seed_new;

  int32_t* update_table_position = &XamRtlRandomTable[param_seed_new & 0x7F];

  int32_t result = *update_table_position;
  *update_table_position = table_seed_new;
  return result;
}

DECLARE_XAM_EXPORT1(RtlRandom, kNone, kImplemented);

dword_result_t Refresh_entry() { return X_ERROR_SUCCESS; }
DECLARE_XAM_EXPORT1(Refresh, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Info);
