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
#include <vector>

#include "xenia/cpu/module.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/util/xex2_info.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace cpu {

class Runtime;

class XexModule : public xe::cpu::Module {
 public:
  XexModule(Processor* processor, kernel::KernelState* kernel_state);
  virtual ~XexModule();

  xe_xex2_ref xex() const { return xex_; }
  bool loaded() const { return loaded_; }
  const xex2_header* xex_header() const {
    return reinterpret_cast<const xex2_header*>(xex_header_mem_.data());
  }
  const xex2_security_info* xex_security_info() const {
    return GetSecurityInfo(xex_header());
  }

  // Gets an optional header. Returns NULL if not found.
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  static bool GetOptHeader(const xex2_header* header, xe_xex2_header_keys key,
                           void** out_ptr);
  bool GetOptHeader(xe_xex2_header_keys key, void** out_ptr) const;

  // Ultra-cool templated version
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  template <typename T>
  static bool GetOptHeader(const xex2_header* header, xe_xex2_header_keys key,
                           T* out_ptr) {
    return GetOptHeader(header, key, reinterpret_cast<void**>(out_ptr));
  }

  template <typename T>
  bool GetOptHeader(xe_xex2_header_keys key, T* out_ptr) const {
    return GetOptHeader(key, reinterpret_cast<void**>(out_ptr));
  }

  static const xex2_security_info* GetSecurityInfo(const xex2_header* header);

  uint32_t GetProcAddress(uint16_t ordinal) const;
  uint32_t GetProcAddress(const char* name) const;

  bool ApplyPatch(XexModule* module);
  bool Load(const std::string& name, const std::string& path,
            const void* xex_addr, size_t xex_length);
  bool Load(const std::string& name, const std::string& path, xe_xex2_ref xex);
  bool Unload();

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint32_t address) override;

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override;

 private:
  bool SetupLibraryImports(const char* name,
                           const xex2_import_library* library);
  bool FindSaveRest();

  Processor* processor_ = nullptr;
  kernel::KernelState* kernel_state_ = nullptr;
  std::string name_;
  std::string path_;
  xe_xex2_ref xex_ = nullptr;
  std::vector<uint8_t> xex_header_mem_;  // Holds the xex header
  bool loaded_ = false;                  // Loaded into memory?

  uint32_t base_address_ = 0;
  uint32_t low_address_ = 0;
  uint32_t high_address_ = 0;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_XEX_MODULE_H_
