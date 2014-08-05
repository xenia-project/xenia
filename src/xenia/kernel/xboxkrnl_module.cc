/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_module.h>

#include <gflags/gflags.h>

#include <xenia/emulator.h>
#include <xenia/export_resolver.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/objects/xuser_module.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


DEFINE_bool(abort_before_entry, false,
    "Abort execution right before launching the module.");


XboxkrnlModule::XboxkrnlModule(Emulator* emulator, KernelState* kernel_state) :
    XKernelModule(kernel_state, "xe:\\xboxkrnl.exe") {
  // Build the export table used for resolution.
  #include <xenia/kernel/util/export_table_pre.inc>
  static KernelExport xboxkrnl_export_table[] = {
    #include <xenia/kernel/xboxkrnl_table.inc>
  };
  #include <xenia/kernel/util/export_table_post.inc>
  export_resolver_->RegisterTable(
      "xboxkrnl.exe", xboxkrnl_export_table, XECOUNT(xboxkrnl_export_table));

  // Register all exported functions.
  RegisterAudioExports(export_resolver_, kernel_state);
  RegisterDebugExports(export_resolver_, kernel_state);
  RegisterHalExports(export_resolver_, kernel_state);
  RegisterIoExports(export_resolver_, kernel_state);
  RegisterMemoryExports(export_resolver_, kernel_state);
  RegisterMiscExports(export_resolver_, kernel_state);
  RegisterModuleExports(export_resolver_, kernel_state);
  RegisterNtExports(export_resolver_, kernel_state);
  RegisterObExports(export_resolver_, kernel_state);
  RegisterRtlExports(export_resolver_, kernel_state_);
  RegisterStringExports(export_resolver_, kernel_state_);
  RegisterThreadingExports(export_resolver_, kernel_state);
  RegisterUsbcamExports(export_resolver_, kernel_state);
  RegisterVideoExports(export_resolver_, kernel_state);

  uint8_t* mem = memory_->membase();

  // KeDebugMonitorData (?*)
  // Set to a valid value when a remote debugger is attached.
  // Offset 0x18 is a 4b pointer to a handler function that seems to take two
  // arguments. If we wanted to see what would happen we could fake that.
  uint32_t pKeDebugMonitorData = (uint32_t)memory_->HeapAlloc(0, 256, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeDebugMonitorData,
      pKeDebugMonitorData);
  poly::store_and_swap<uint32_t>(mem + pKeDebugMonitorData, 0);

  // KeCertMonitorData (?*)
  // Always set to zero, ignored.
  uint32_t pKeCertMonitorData = (uint32_t)memory_->HeapAlloc(0, 4, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeCertMonitorData,
      pKeCertMonitorData);
  poly::store_and_swap<uint32_t>(mem + pKeCertMonitorData, 0);

  // XboxHardwareInfo (XboxHardwareInfo_t, 16b)
  // flags       cpu#  ?     ?     ?     ?           ?       ?
  // 0x00000000, 0x06, 0x00, 0x00, 0x00, 0x00000000, 0x0000, 0x0000
  // Games seem to check if bit 26 (0x20) is set, which at least for xbox1
  // was whether an HDD was present. Not sure what the other flags are.
  uint32_t pXboxHardwareInfo = (uint32_t)memory_->HeapAlloc(0, 16, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxHardwareInfo,
      pXboxHardwareInfo);
  poly::store_and_swap<uint32_t>(mem + pXboxHardwareInfo + 0, 0x00000000); // flags
  poly::store_and_swap<uint8_t> (mem + pXboxHardwareInfo + 4, 0x06);       // cpu count
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
  uint32_t ppXexExecutableModuleHandle = (uint32_t)memory_->HeapAlloc(0, 4, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XexExecutableModuleHandle,
      ppXexExecutableModuleHandle);
  uint32_t pXexExecutableModuleHandle =
      (uint32_t)memory_->HeapAlloc(0, 256, 0);
  poly::store_and_swap<uint32_t>(mem + ppXexExecutableModuleHandle, pXexExecutableModuleHandle);
  poly::store_and_swap<uint32_t>(mem + pXexExecutableModuleHandle + 0x58, 0x80101100);

  // ExLoadedCommandLine (char*)
  // The name of the xex. Not sure this is ever really used on real devices.
  // Perhaps it's how swap disc/etc data is sent?
  // Always set to "default.xex" (with quotes) for now.
  uint32_t pExLoadedCommandLine = (uint32_t)memory_->HeapAlloc(0, 1024, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExLoadedCommandLine,
      pExLoadedCommandLine);
  char command_line[] = "\"default.xex\"";
  xe_copy_memory(mem + pExLoadedCommandLine, 1024,
                 command_line, XECOUNT(command_line) + 1);

  // XboxKrnlVersion (8b)
  // Kernel version, looks like 2b.2b.2b.2b.
  // I've only seen games check >=, so we just fake something here.
  uint32_t pXboxKrnlVersion = (uint32_t)memory_->HeapAlloc(0, 8, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxKrnlVersion,
      pXboxKrnlVersion);
  poly::store_and_swap<uint16_t>(mem + pXboxKrnlVersion + 0, 2);
  poly::store_and_swap<uint16_t>(mem + pXboxKrnlVersion + 2, 0xFFFF);
  poly::store_and_swap<uint16_t>(mem + pXboxKrnlVersion + 4, 0xFFFF);
  poly::store_and_swap<uint16_t>(mem + pXboxKrnlVersion + 6, 0xFFFF);

  // KeTimeStampBundle (ad)
  uint32_t pKeTimeStampBundle = (uint32_t)memory_->HeapAlloc(0, 24, 0);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeTimeStampBundle,
      pKeTimeStampBundle);
  poly::store_and_swap<uint64_t>(mem + pKeTimeStampBundle + 0,  0);
  poly::store_and_swap<uint64_t>(mem + pKeTimeStampBundle + 8,  0);
  poly::store_and_swap<uint32_t>(mem + pKeTimeStampBundle + 12, 0);
}

XboxkrnlModule::~XboxkrnlModule() {
}

int XboxkrnlModule::LaunchModule(const char* path) {
  // Create and register the module. We keep it local to this function and
  // dispose it on exit.
  XUserModule* module = new XUserModule(kernel_state_, path);

  // Load the module into memory from the filesystem.
  X_STATUS result_code = module->LoadFromFile(path);
  if (XFAILED(result_code)) {
    XELOGE("Failed to load module %s: %.8X", path, result_code);
    module->Release();
    return 1;
  }

  // Set as the main module, while running.
  kernel_state_->SetExecutableModule(module);

  if (FLAGS_abort_before_entry) {
    XELOGI("--abort_before_entry causing an early exit");
    module->Release();
    return 0;
  }

  // Launch the module.
  // NOTE: this won't return until the module exits.
  result_code = module->Launch(0);
  kernel_state_->SetExecutableModule(NULL);
  if (XFAILED(result_code)) {
    XELOGE("Failed to launch module %s: %.8X", path, result_code);
    module->Release();
    return 2;
  }

  module->Release();

  return 0;
}
