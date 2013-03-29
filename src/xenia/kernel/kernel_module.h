/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_MODULE_H_
#define XENIA_KERNEL_KERNEL_MODULE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/runtime.h>


namespace xe {
namespace kernel {


class Runtime;


class KernelModule {
public:
  KernelModule(Runtime* runtime) {
    runtime_  = runtime;
    memory_   = runtime->memory();
    export_resolver_ = runtime->export_resolver();
  }

  virtual ~KernelModule() {
    export_resolver_.reset();
    xe_memory_release(memory_);
  }

protected:
  Runtime*        runtime_;
  xe_memory_ref   memory_;
  shared_ptr<ExportResolver> export_resolver_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_KERNEL_MODULE_H_
