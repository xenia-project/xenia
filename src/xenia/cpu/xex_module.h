/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_XEX_MODULE_H_
#define XENIA_CPU_XEX_MODULE_H_

#include <string>

#include "xenia/cpu/module.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/util/xex2_info.h"

namespace xe {

// KernelState forward decl.
namespace kernel {
class KernelState;
}

namespace cpu {

class Runtime;

class XexModule : public xe::cpu::Module {
 public:
  XexModule(Processor* processor, kernel::KernelState* kernel_state);
  virtual ~XexModule();

  xe_xex2_ref xex() const { return xex_; }
  const xex2_header* xex_header() const { return xex_header_; }

  // Gets an optional header. Returns NULL if not found.
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer!
  static bool GetOptHeader(const xex2_header* header, xe_xex2_header_keys key,
                           void** out_ptr);
  bool GetOptHeader(xe_xex2_header_keys key, void** out_ptr) const;

  uint32_t GetProcAddress(uint16_t ordinal) const;
  uint32_t GetProcAddress(const char* name) const;

  bool ApplyPatch(XexModule* module);
  bool Load(const std::string& name, const std::string& path,
            const void* xex_addr, size_t xex_length);
  bool Load(const std::string& name, const std::string& path, xe_xex2_ref xex);

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint32_t address) override;

 private:
  bool SetupImports(xe_xex2_ref xex);
  bool SetupLibraryImports(const xe_xex2_import_library_t* library);
  bool FindSaveRest();

 private:
  Processor* processor_;
  kernel::KernelState* kernel_state_;
  std::string name_;
  std::string path_;
  xe_xex2_ref xex_;
  xex2_header* xex_header_;

  uint32_t base_address_;
  uint32_t low_address_;
  uint32_t high_address_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_XEX_MODULE_H_
