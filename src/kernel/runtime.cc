/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/runtime.h>

#include "kernel/modules/modules.h"


using namespace xe;
using namespace xe::kernel;


Runtime::Runtime(xe_pal_ref pal, shared_ptr<cpu::Processor> processor,
                 const xechar_t* command_line) {
  pal_ = xe_pal_retain(pal);
  memory_ = processor->memory();
  processor_ = processor;
  XEIGNORE(xestrcpy(command_line_, XECOUNT(command_line_), command_line));
  export_resolver_ = shared_ptr<ExportResolver>(new ExportResolver());

  kernel_modules_.push_back(
      new xboxkrnl::XboxkrnlModule(pal_, memory_, export_resolver_));
  kernel_modules_.push_back(
      new xbdm::XbdmModule(pal_, memory_, export_resolver_));
  kernel_modules_.push_back(
      new xam::XamModule(pal_, memory_, export_resolver_));
}

Runtime::~Runtime() {
  for (std::map<const xechar_t*, UserModule*>::iterator it =
       user_modules_.begin(); it != user_modules_.end(); ++it) {
    delete it->second;
  }
  for (std::vector<KernelModule*>::iterator it = kernel_modules_.begin();
       it != kernel_modules_.end(); ++it) {
    delete *it;
  }

  xe_memory_release(memory_);
  xe_pal_release(pal_);
}

xe_pal_ref Runtime::pal() {
  return xe_pal_retain(pal_);
}

xe_memory_ref Runtime::memory() {
  return xe_memory_retain(memory_);
}

shared_ptr<cpu::Processor> Runtime::processor() {
  return processor_;
}

shared_ptr<ExportResolver> Runtime::export_resolver() {
  return export_resolver_;
}

const xechar_t* Runtime::command_line() {
  return command_line_;
}

int Runtime::LoadModule(const xechar_t* path) {
  if (GetModule(path)) {
    return 0;
  }

  // TODO(benvanik): map file from filesystem
  xe_mmap_ref mmap = xe_mmap_open(pal_, kXEFileModeRead, path, 0, 0);
  if (!mmap) {
    return NULL;
  }
  void* addr = xe_mmap_get_addr(mmap);
  size_t length = xe_mmap_get_length(mmap);

  UserModule* module = new UserModule(memory_);
  int result_code = module->Load(addr, length, path);

  // TODO(benvanik): retain memory somehow? is it needed?
  xe_mmap_release(mmap);

  if (result_code) {
    delete module;
    return 1;
  }

  // Prepare the module.
  XEEXPECTZERO(processor_->PrepareModule(module, export_resolver_));

  // Stash in modules list (takes reference).
  user_modules_.insert(std::pair<const xechar_t*, UserModule*>(path, module));

  return 0;

XECLEANUP:
  delete module;
  return 1;
}

void Runtime::LaunchModule(UserModule* user_module) {
  //const xe_xex2_header_t *xex_header = xe_module_get_xex_header(module);

  // TODO(benvanik): set as main module/etc
  // xekXexExecutableModuleHandle = xe_module_get_handle(module);

  // XEEXPECTTRUE(XECPUPrepareModule(XEGetCPU(), module->xex, module->pe, module->address_space, module->address_space_size));

  // Setup the heap (and TLS?).
  // xex_header->exe_heap_size;

  // Launch thread.
  // XHANDLE thread_handle;
  // XDWORD thread_id;
  // XBOOL result = xekExCreateThread(&thread_handle, xex_header->exe_stack_size, &thread_id, NULL, (void*)xex_header->exe_entry_point, NULL, 0);

  // Wait until thread completes.
  // XLARGE_INTEGER timeout = XINFINITE;
  // xekNtWaitForSingleObjectEx(thread_handle, TRUE, &timeout);
}

UserModule* Runtime::GetModule(const xechar_t* path) {
  std::map<const xechar_t*, UserModule*>::iterator it =
      user_modules_.find(path);
  if (it != user_modules_.end()) {
    return it->second;
  }
  return NULL;
}

void Runtime::UnloadModule(UserModule* user_module) {
  // TODO(benvanik): unload module
  XEASSERTALWAYS();
}
