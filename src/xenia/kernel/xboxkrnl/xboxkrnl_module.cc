/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"

#include <gflags/gflags.h>

#include <vector>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

XboxkrnlModule::XboxkrnlModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xboxkrnl.exe"),
      timestamp_timer_(nullptr) {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
  RegisterAudioExports(export_resolver_, kernel_state_);
  RegisterAudioXmaExports(export_resolver_, kernel_state_);
  RegisterCryptExports(export_resolver_, kernel_state_);
  RegisterDebugExports(export_resolver_, kernel_state_);
  RegisterErrorExports(export_resolver_, kernel_state_);
  RegisterHalExports(export_resolver_, kernel_state_);
  RegisterIoExports(export_resolver_, kernel_state_);
  RegisterMemoryExports(export_resolver_, kernel_state_);
  RegisterMiscExports(export_resolver_, kernel_state_);
  RegisterModuleExports(export_resolver_, kernel_state_);
  RegisterObExports(export_resolver_, kernel_state_);
  RegisterRtlExports(export_resolver_, kernel_state_);
  RegisterStringExports(export_resolver_, kernel_state_);
  RegisterThreadingExports(export_resolver_, kernel_state_);
  RegisterUsbcamExports(export_resolver_, kernel_state_);
  RegisterVideoExports(export_resolver_, kernel_state_);

  // KeDebugMonitorData (?*)
  // Set to a valid value when a remote debugger is attached.
  // Offset 0x18 is a 4b pointer to a handler function that seems to take two
  // arguments. If we wanted to see what would happen we could fake that.
  uint32_t pKeDebugMonitorData = memory_->SystemHeapAlloc(256);
  auto lpKeDebugMonitorData = memory_->TranslateVirtual(pKeDebugMonitorData);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeDebugMonitorData, pKeDebugMonitorData);
  xe::store_and_swap<uint32_t>(lpKeDebugMonitorData, 0);

  // KeCertMonitorData (?*)
  // Always set to zero, ignored.
  uint32_t pKeCertMonitorData = memory_->SystemHeapAlloc(4);
  auto lpKeCertMonitorData = memory_->TranslateVirtual(pKeCertMonitorData);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeCertMonitorData, pKeCertMonitorData);
  xe::store_and_swap<uint32_t>(lpKeCertMonitorData, 0);

  // XboxHardwareInfo (XboxHardwareInfo_t, 16b)
  // flags       cpu#  ?     ?     ?     ?           ?       ?
  // 0x00000000, 0x06, 0x00, 0x00, 0x00, 0x00000000, 0x0000, 0x0000
  // Games seem to check if bit 26 (0x20) is set, which at least for xbox1
  // was whether an HDD was present. Not sure what the other flags are.
  //
  // aomega08 says the value is 0x02000817, bit 27: debug mode on.
  // When that is set, though, allocs crash in weird ways.
  uint32_t pXboxHardwareInfo = memory_->SystemHeapAlloc(16);
  auto lpXboxHardwareInfo = memory_->TranslateVirtual(pXboxHardwareInfo);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxHardwareInfo, pXboxHardwareInfo);
  xe::store_and_swap<uint32_t>(lpXboxHardwareInfo + 0, 0);    // flags
  xe::store_and_swap<uint8_t>(lpXboxHardwareInfo + 4, 0x06);  // cpu count
  // Remaining 11b are zeroes?

  // XexExecutableModuleHandle (?**)
  // Games try to dereference this to get a pointer to some module struct.
  // So far it seems like it's just in loader code, and only used to look up
  // the XexHeaderBase for use by RtlImageXexHeaderField.
  // We fake it so that the address passed to that looks legit.
  // 0x80100FFC <- pointer to structure
  // 0x80101000 <- our module structure
  // 0x80101058 <- pointer to xex header
  // 0x80101100 <- xex header base
  uint32_t ppXexExecutableModuleHandle = memory_->SystemHeapAlloc(4);
  export_resolver_->SetVariableMapping("xboxkrnl.exe",
                                       ordinals::XexExecutableModuleHandle,
                                       ppXexExecutableModuleHandle);

  // ExLoadedCommandLine (char*)
  // The name of the xex. Not sure this is ever really used on real devices.
  // Perhaps it's how swap disc/etc data is sent?
  // Always set to "default.xex" (with quotes) for now.
  uint32_t pExLoadedCommandLine = memory_->SystemHeapAlloc(1024);
  auto lpExLoadedCommandLine = memory_->TranslateVirtual(pExLoadedCommandLine);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExLoadedCommandLine, pExLoadedCommandLine);
  char command_line[] = "\"default.xex\"";
  std::memcpy(lpExLoadedCommandLine, command_line,
              xe::countof(command_line) + 1);

  // XboxKrnlVersion (8b)
  // Kernel version, looks like 2b.2b.2b.2b.
  // I've only seen games check >=, so we just fake something here.
  uint32_t pXboxKrnlVersion = memory_->SystemHeapAlloc(8);
  auto lpXboxKrnlVersion = memory_->TranslateVirtual(pXboxKrnlVersion);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxKrnlVersion, pXboxKrnlVersion);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 0, 2);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 2, 0xFFFF);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 4, 0xFFFF);
  xe::store_and_swap<uint8_t>(lpXboxKrnlVersion + 6, 0x80);
  xe::store_and_swap<uint8_t>(lpXboxKrnlVersion + 7, 0x00);

  // KeTimeStampBundle (ad)
  // This must be updated during execution, at 1ms intevals.
  // We setup a system timer here to do that.
  uint32_t pKeTimeStampBundle = memory_->SystemHeapAlloc(24);
  auto lpKeTimeStampBundle = memory_->TranslateVirtual(pKeTimeStampBundle);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeTimeStampBundle, pKeTimeStampBundle);
  xe::store_and_swap<uint64_t>(lpKeTimeStampBundle + 0, 0);
  xe::store_and_swap<uint64_t>(lpKeTimeStampBundle + 8, 0);
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 16,
                               Clock::QueryGuestUptimeMillis());
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 20, 0);
  timestamp_timer_ = xe::threading::HighResolutionTimer::CreateRepeating(
      std::chrono::milliseconds(1), [lpKeTimeStampBundle]() {
        xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 16,
                                     Clock::QueryGuestUptimeMillis());
      });
}

std::vector<xe::cpu::Export*> xboxkrnl_exports(4096);

xe::cpu::Export* RegisterExport_xboxkrnl(xe::cpu::Export* export_entry) {
  assert_true(export_entry->ordinal < xboxkrnl_exports.size());
  xboxkrnl_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XboxkrnlModule::RegisterExportTable(
    xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xboxkrnl_export_table[] = {
#include "xenia/kernel/xboxkrnl/xboxkrnl_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  for (size_t i = 0; i < xe::countof(xboxkrnl_export_table); ++i) {
    auto& export_entry = xboxkrnl_export_table[i];
    assert_true(export_entry.ordinal < xboxkrnl_exports.size());
    if (!xboxkrnl_exports[export_entry.ordinal]) {
      xboxkrnl_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xboxkrnl.exe", &xboxkrnl_exports);
}

XboxkrnlModule::~XboxkrnlModule() = default;

int XboxkrnlModule::LaunchModule(const char* path) {
  // Create and register the module. We keep it local to this function and
  // dispose it on exit.
  auto module = kernel_state_->LoadUserModule(path);
  if (!module) {
    XELOGE("Failed to load user module %s.", path);
    return 2;
  }

  // Set as the main module, while running.
  kernel_state_->SetExecutableModule(module);

  // Launch the module.
  // NOTE: this won't return until the module exits.
  X_STATUS result_code = module->Launch(0);

  // Main thread exited. Terminate the title.
  kernel_state_->TerminateTitle();
  kernel_state_->SetExecutableModule(NULL);
  if (XFAILED(result_code)) {
    XELOGE("Failed to launch module %s: %.8X", path, result_code);
    return 2;
  }

  return 0;
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
