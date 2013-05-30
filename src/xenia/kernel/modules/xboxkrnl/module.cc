/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/module.h>

#include <gflags/gflags.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


DEFINE_bool(abort_before_entry, false,
    "Abort execution right before launching the module.");


KernelState* xe::kernel::xboxkrnl::shared_kernel_state_ = NULL;


XboxkrnlModule::XboxkrnlModule(Runtime* runtime) :
    KernelModule(runtime) {
  ExportResolver* resolver = export_resolver_.get();

  // Build the export table used for resolution.
  #include <xenia/kernel/util/export_table_pre.inc>
  static KernelExport xboxkrnl_export_table[] = {
    #include <xenia/kernel/modules/xboxkrnl/xboxkrnl_table.inc>
  };
  #include <xenia/kernel/util/export_table_post.inc>
  resolver->RegisterTable(
      "xboxkrnl.exe", xboxkrnl_export_table, XECOUNT(xboxkrnl_export_table));

  // Setup the kernel state instance.
  // This is where all kernel objects are kept while running.
  kernel_state_ = auto_ptr<KernelState>(new KernelState(runtime));

  // Setup the shared global state object.
  XEASSERTNULL(shared_kernel_state_);
  shared_kernel_state_ = kernel_state_.get();

  // Register all exported functions.
  RegisterHalExports(resolver, kernel_state_.get());
  RegisterMemoryExports(resolver, kernel_state_.get());
  RegisterModuleExports(resolver, kernel_state_.get());
  RegisterRtlExports(resolver, kernel_state_.get());
  RegisterThreadingExports(resolver, kernel_state_.get());
  RegisterVideoExports(resolver, kernel_state_.get());

  uint8_t* mem = xe_memory_addr(memory_);

  // KeDebugMonitorData (?*)
  // Set to a valid value when a remote debugger is attached.
  // Offset 0x18 is a 4b pointer to a handler function that seems to take two
  // arguments. If we wanted to see what would happen we could fake that.
  uint32_t pKeDebugMonitorData = xe_memory_heap_alloc(memory_, 0, 256, 0);
  resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeDebugMonitorData,
      pKeDebugMonitorData);
  XESETUINT32BE(mem + pKeDebugMonitorData, 0);

  // XboxHardwareInfo (XboxHardwareInfo_t, 16b)
  // flags       cpu#  ?     ?     ?     ?           ?       ?
  // 0x00000000, 0x06, 0x00, 0x00, 0x00, 0x00000000, 0x0000, 0x0000
  // Games seem to check if bit 26 (0x20) is set, which at least for xbox1
  // was whether an HDD was present. Not sure what the other flags are.
  uint32_t pXboxHardwareInfo = xe_memory_heap_alloc(memory_, 0, 16, 0);
  resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxHardwareInfo,
      pXboxHardwareInfo);
  XESETUINT32BE(mem + pXboxHardwareInfo + 0, 0x00000000); // flags
  XESETUINT8BE (mem + pXboxHardwareInfo + 4, 0x06);       // cpu count
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
  uint32_t ppXexExecutableModuleHandle =
      xe_memory_heap_alloc(memory_, 0, 4, 0);
  resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XexExecutableModuleHandle,
      ppXexExecutableModuleHandle);
  uint32_t pXexExecutableModuleHandle =
      xe_memory_heap_alloc(memory_, 0, 256, 0);
  XESETUINT32BE(mem + ppXexExecutableModuleHandle, pXexExecutableModuleHandle);
  XESETUINT32BE(mem + pXexExecutableModuleHandle + 0x58, 0x80101100);

  // ExLoadedCommandLine (char*)
  // The name of the xex. Not sure this is ever really used on real devices.
  // Perhaps it's how swap disc/etc data is sent?
  // Always set to "default.xex" (with quotes) for now.
  uint32_t pExLoadedCommandLine = xe_memory_heap_alloc(memory_, 0, 1024, 0);
  resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExLoadedCommandLine,
      pExLoadedCommandLine);
  char command_line[] = "\"default.xex\"";
  xe_copy_memory(mem + pExLoadedCommandLine, 1024,
                 command_line, XECOUNT(command_line) + 1);
}

XboxkrnlModule::~XboxkrnlModule() {
  // Clear the shared kernel state.
  shared_kernel_state_ = NULL;
}

int XboxkrnlModule::LaunchModule(const char* path) {
  // Create and register the module. We keep it local to this function and
  // dispose it on exit.
  XModule* module = new XModule(kernel_state_.get(), path);

  // Load the module into memory from the filesystem.
  X_STATUS result_code = module->LoadFromFile(path);
  if (XFAILED(result_code)) {
    XELOGE("Failed to load module %s: %.8X", path, result_code);
    module->Release();
    return 1;
  }

  if (FLAGS_abort_before_entry) {
    XELOGI("--abort_before_entry causing an early exit");
    module->Release();
    return 0;
  }

  // Launch the module.
  // NOTE: this won't return until the module exits.
  result_code = module->Launch(0);
  if (XFAILED(result_code)) {
    XELOGE("Failed to launch module %s: %.8X", path, result_code);
    module->Release();
    return 2;
  }

  module->Release();

  return 0;
}
