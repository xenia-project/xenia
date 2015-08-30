/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_OBJECTS_XUSER_MODULE_H_
#define XENIA_KERNEL_OBJECTS_XUSER_MODULE_H_

#include <string>

#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/kernel/objects/xmodule.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/xbox.h"

namespace xe {
namespace cpu {
class XexModule;
class ElfModule;
}  // namespace cpu

namespace kernel {

class XUserModule : public XModule {
 public:
  XUserModule(KernelState* kernel_state, const char* path);
  ~XUserModule() override;

  enum ModuleFormat {
    kModuleFormatUndefined = 0,
    kModuleFormatXex,
    kModuleFormatElf,
  };

  const xe::cpu::XexModule* xex_module() const {
    assert_true(module_format_ == kModuleFormatXex);
    return reinterpret_cast<xe::cpu::XexModule*>(processor_module_);
  }
  xe::cpu::XexModule* xex_module() {
    assert_true(module_format_ == kModuleFormatXex);
    return reinterpret_cast<xe::cpu::XexModule*>(processor_module_);
  }

  const xex2_header* xex_header() const { return xex_module()->xex_header(); }
  uint32_t guest_xex_header() const { return guest_xex_header_; }
  bool dll_module() const { return dll_module_; }

  uint32_t entry_point() const { return entry_point_; }
  uint32_t stack_size() const { return stack_size_; }

  X_STATUS LoadFromFile(std::string path);
  X_STATUS LoadFromMemory(const void* addr, const size_t length);
  X_STATUS Unload();

  uint32_t GetProcAddressByOrdinal(uint16_t ordinal) override;
  uint32_t GetProcAddressByName(const char* name) override;
  X_STATUS GetSection(const char* name, uint32_t* out_section_data,
                      uint32_t* out_section_size) override;

  // Get optional header - FOR HOST USE ONLY!
  X_STATUS GetOptHeader(xe_xex2_header_keys key, void** out_ptr);

  // Get optional header - FOR HOST USE ONLY!
  template <typename T>
  X_STATUS GetOptHeader(xe_xex2_header_keys key, T* out_ptr) {
    return GetOptHeader(key, reinterpret_cast<void**>(out_ptr));
  }

  // Get optional header that can safely be returned to guest code.
  X_STATUS GetOptHeader(xe_xex2_header_keys key,
                        uint32_t* out_header_guest_ptr);
  static X_STATUS GetOptHeader(uint8_t* membase, const xex2_header* header,
                               xe_xex2_header_keys key,
                               uint32_t* out_header_guest_ptr);

  X_STATUS Launch(uint32_t flags);

  void Dump();

 private:
  uint32_t guest_xex_header_ = 0;
  ModuleFormat module_format_ = kModuleFormatUndefined;

  bool dll_module_;
  uint32_t entry_point_;
  uint32_t stack_size_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_OBJECTS_XUSER_MODULE_H_
