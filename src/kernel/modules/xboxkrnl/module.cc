/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/module.h"

#include "kernel/modules/xboxkrnl/kernel_state.h"
#include "kernel/modules/xboxkrnl/objects/xmodule.h"

#include "kernel/modules/xboxkrnl/xboxkrnl_hal.h"
#include "kernel/modules/xboxkrnl/xboxkrnl_memory.h"
#include "kernel/modules/xboxkrnl/xboxkrnl_module.h"
#include "kernel/modules/xboxkrnl/xboxkrnl_rtl.h"
#include "kernel/modules/xboxkrnl/xboxkrnl_threading.h"

#include "kernel/modules/xboxkrnl/xboxkrnl_table.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {

}


XboxkrnlModule::XboxkrnlModule(Runtime* runtime) :
    KernelModule(runtime) {
  ExportResolver* resolver = export_resolver_.get();

  resolver->RegisterTable(
      "xboxkrnl.exe", xboxkrnl_export_table, XECOUNT(xboxkrnl_export_table));

  // Setup the kernel state instance.
  // This is where all kernel objects are kept while running.
  kernel_state_ = auto_ptr<KernelState>(new KernelState(runtime));

  // Register all exported functions.
  RegisterHalExports(resolver, kernel_state_.get());
  RegisterMemoryExports(resolver, kernel_state_.get());
  RegisterModuleExports(resolver, kernel_state_.get());
  RegisterRtlExports(resolver, kernel_state_.get());
  RegisterThreadingExports(resolver, kernel_state_.get());

  // TODO(benvanik): alloc heap memory somewhere in user space
  // TODO(benvanik): tools for reading/writing to heap memory

  uint8_t* mem = xe_memory_addr(memory_, 0);

  // KeDebugMonitorData (?*)
  // I'm not sure what this is for, but games make sure it's not null and
  // exit if it is.
  resolver->SetVariableMapping(
      "xboxkrnl.exe", 0x00000059,
      0x80102100);
  XESETUINT32BE(mem + 0x80102100, 0x1);

  // XboxHardwareInfo (XboxHardwareInfo_t, 16b)
  // flags       cpu#  ?     ?     ?     ?           ?       ?
  // 0x00000000, 0x06, 0x00, 0x00, 0x00, 0x00000000, 0x0000, 0x0000
  // Games seem to check if bit 26 (0x20) is set, which at least for xbox1
  // was whether an HDD was present. Not sure what the other flags are.
  resolver->SetVariableMapping(
      "xboxkrnl.exe", 0x00000156,
      0x80100FED);
  XESETUINT32BE(mem + 0x80100FED, 0x00000000);  // flags
  XESETUINT8BE(mem  + 0x80100FEE, 0x06);        // cpu count
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
  resolver->SetVariableMapping(
      "xboxkrnl.exe", 0x00000193,
      0x80100FFC);
  XESETUINT32BE(mem + 0x80100FFC, 0x80101000);
  XESETUINT32BE(mem + 0x80101058, 0x80101100);

  // ExLoadedCommandLine (char*)
  // The name of the xex. Not sure this is ever really used on real devices.
  // Perhaps it's how swap disc/etc data is sent?
  // Always set to "default.xex" (with quotes) for now.
  resolver->SetVariableMapping(
      "xboxkrnl.exe", 0x000001AE,
      0x80102000);
  char command_line[] = "\"default.xex\"";
  xe_copy_memory(mem + 0x80102000, 1024,
                 command_line, XECOUNT(command_line) + 1);
}

XboxkrnlModule::~XboxkrnlModule() {
}

int XboxkrnlModule::LaunchModule(const xechar_t* path) {
  // TODO(benvanik): setup the virtual filesystem map and use LoadFromFile.

  xe_mmap_ref mmap = xe_mmap_open(pal_, kXEFileModeRead, path, 0, 0);
  if (!mmap) {
    return NULL;
  }
  void* addr = xe_mmap_get_addr(mmap);
  size_t length = xe_mmap_get_length(mmap);

  char path_a[2048];
  XEIGNORE(xestrnarrow(path_a, XECOUNT(path_a), path));

  // Create and load the module.
  XModule* module = new XModule(kernel_state_.get(), path_a);
  X_STATUS status = module->LoadFromMemory(addr, length);

  // TODO(benvanik): retain memory somehow? is it needed?
  xe_mmap_release(mmap);

  if (XFAILED(status)) {
    XELOGE(XT("Failed to load module"));
    module->Release();
    return 1;
  }

  // Launch the module.
  status = module->Launch(0);
  if (XFAILED(status)) {
    XELOGE(XT("Failed to launch module"));
    module->Release();
    return 2;
  }

  module->Release();

  return 0;
}
