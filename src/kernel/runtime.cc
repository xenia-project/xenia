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
using namespace xe::cpu;
using namespace xe::kernel;


Runtime::Runtime(xe_pal_ref pal, shared_ptr<cpu::Processor> processor,
                 const xechar_t* command_line) {
  pal_ = xe_pal_retain(pal);
  memory_ = processor->memory();
  processor_ = processor;
  XEIGNORE(xestrcpy(command_line_, XECOUNT(command_line_), command_line));
  export_resolver_ = shared_ptr<ExportResolver>(new ExportResolver());

  xboxkrnl_ = auto_ptr<xboxkrnl::XboxkrnlModule>(
      new xboxkrnl::XboxkrnlModule(this));
  xam_ = auto_ptr<xam::XamModule>(
      new xam::XamModule(this));
}

Runtime::~Runtime() {
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

int Runtime::LaunchModule(const xechar_t* path) {
  return xboxkrnl_->LaunchModule(path);
}
