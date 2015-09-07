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

#include "xenia/emulator.h"
#include "xenia/kernel/xmodule.h"

namespace xe {
namespace kernel {

class KernelState;

class KernelModule : public XModule {
 public:
  KernelModule(KernelState* kernel_state, const char* path);
  ~KernelModule() override;

  uint32_t GetProcAddressByOrdinal(uint16_t ordinal) override;
  uint32_t GetProcAddressByName(const char* name) override;

 protected:
  Emulator* emulator_;
  Memory* memory_;
  xe::cpu::ExportResolver* export_resolver_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_MODULE_H_
