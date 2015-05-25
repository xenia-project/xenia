/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl_module.h"

#include <gflags/gflags.h>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/kernel/objects/xuser_module.h"

namespace xe {
namespace kernel {

XboxkrnlModule::XboxkrnlModule(Emulator* emulator, KernelState* kernel_state)
    : XKernelModule(kernel_state, "xe:\\xboxkrnl.exe"),
      timestamp_timer_(nullptr) {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
  xboxkrnl::RegisterAudioExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterAudioXmaExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterDebugExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterErrorExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterHalExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterIoExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterMemoryExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterMiscExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterModuleExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterObExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterRtlExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterStringExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterThreadingExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterUsbcamExports(export_resolver_, kernel_state_);
  xboxkrnl::RegisterVideoExports(export_resolver_, kernel_state_);

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
  auto lppXexExecutableModuleHandle =
      memory_->TranslateVirtual(ppXexExecutableModuleHandle);
  export_resolver_->SetVariableMapping("xboxkrnl.exe",
                                       ordinals::XexExecutableModuleHandle,
                                       ppXexExecutableModuleHandle);
  uint32_t pXexExecutableModuleHandle = memory_->SystemHeapAlloc(256);
  auto lpXexExecutableModuleHandle =
      memory_->TranslateVirtual(pXexExecutableModuleHandle);
  xe::store_and_swap<uint32_t>(lppXexExecutableModuleHandle,
                               pXexExecutableModuleHandle);
  xe::store_and_swap<uint32_t>(lpXexExecutableModuleHandle + 0x58, 0x80101100);

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
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 16, GetTickCount());
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 20, 0);
  CreateTimerQueueTimer(
      &timestamp_timer_, nullptr,
      [](PVOID param, BOOLEAN timer_or_wait_fired) {
        auto timestamp_bundle = reinterpret_cast<uint8_t*>(param);
        xe::store_and_swap<uint32_t>(timestamp_bundle + 16, GetTickCount());
      },
      lpKeTimeStampBundle, 0,
      1,  // 1ms
      WT_EXECUTEINTIMERTHREAD);
}

void XboxkrnlModule::RegisterExportTable(
    xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

  if (!export_resolver) {
    return;
  }

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::KernelExport xboxkrnl_export_table[] = {
#include "xenia/kernel/xboxkrnl_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  export_resolver->RegisterTable("xboxkrnl.exe", xboxkrnl_export_table,
                                 xe::countof(xboxkrnl_export_table));
}

XboxkrnlModule::~XboxkrnlModule() {
  DeleteTimerQueueTimer(nullptr, timestamp_timer_, nullptr);
}

int XboxkrnlModule::LaunchModule(const char* path) {
  // Create and register the module. We keep it local to this function and
  // dispose it on exit.
  auto module = object_ref<XUserModule>(new XUserModule(kernel_state_, path));

  // Load the module into memory from the filesystem.
  X_STATUS result_code = module->LoadFromFile(path);
  if (XFAILED(result_code)) {
    XELOGE("Failed to load module %s: %.8X", path, result_code);
    return 1;
  }

  // Set as the main module, while running.
  kernel_state_->SetExecutableModule(module);

  // Waits for a debugger client, if desired.
  emulator()->debugger()->PreLaunch();

  // Launch the module.
  // NOTE: this won't return until the module exits.
  result_code = module->Launch(0);
  kernel_state_->SetExecutableModule(NULL);
  if (XFAILED(result_code)) {
    XELOGE("Failed to launch module %s: %.8X", path, result_code);
    return 2;
  }

  return 0;
}

}  // namespace kernel
}  // namespace xe
