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
namespace cpu {
class RawModule;
}  // namespace cpu

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

  // Guest trampoline for GetProcAddress
  static const uint32_t kTrampolineSize = 400 * 8;
  uint32_t guest_trampoline_ = 0;
  uint32_t guest_trampoline_next_ = 0;  // Next free entry to be generated.
  uint32_t guest_trampoline_size_ = 0;
  cpu::RawModule* guest_trampoline_module_ = nullptr;
  std::map<uint16_t, uint32_t> guest_trampoline_map_;

  xe::global_critical_region global_critical_region_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_MODULE_H_
