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
  struct ImportLibraryFn {
   public:
    uint32_t Ordinal;
    uint32_t ValueAddress;
    uint32_t ThunkAddress;
  };
  struct ImportLibrary {
   public:
    std::string Name;
    uint32_t ID;
    xe_xex2_version_t Version;
    xe_xex2_version_t MinVersion;
    std::vector<ImportLibraryFn> Imports;
  };

  XexModule(Processor* processor, kernel::KernelState* kernel_state);
  virtual ~XexModule();

  bool loaded() const { return loaded_; }
  const xex2_header* xex_header() const {
    return reinterpret_cast<const xex2_header*>(xex_header_mem_.data());
  }
  const xex2_security_info* xex_security_info() const {
    return reinterpret_cast<const xex2_security_info*>(
        uintptr_t(xex_header()) + xex_header()->security_offset);
  }

  const std::vector<ImportLibrary>* import_libraries() const {
    return &import_libs_;
  }

  const xex2_opt_execution_info* opt_execution_info() const {
    xex2_opt_execution_info* retval = nullptr;
    GetOptHeader(XEX_HEADER_EXECUTION_INFO, &retval);
    return retval;
  }

  const xex2_opt_file_format_info* opt_file_format_info() const {
    xex2_opt_file_format_info* retval = nullptr;
    GetOptHeader(XEX_HEADER_FILE_FORMAT_INFO, &retval);
    return retval;
  }

  const uint32_t base_address() const { return base_address_; }

  // Gets an optional header. Returns NULL if not found.
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  static bool GetOptHeader(const xex2_header* header, xex2_header_keys key,
                           void** out_ptr);
  bool GetOptHeader(xex2_header_keys key, void** out_ptr) const;

  // Ultra-cool templated version
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  template <typename T>
  static bool GetOptHeader(const xex2_header* header, xex2_header_keys key,
                           T* out_ptr) {
    return GetOptHeader(header, key, reinterpret_cast<void**>(out_ptr));
  }

  template <typename T>
  bool GetOptHeader(xex2_header_keys key, T* out_ptr) const {
    return GetOptHeader(key, reinterpret_cast<void**>(out_ptr));
  }

  const PESection* GetPESection(const char* name);

  uint32_t GetProcAddress(uint16_t ordinal) const;
  uint32_t GetProcAddress(const char* name) const;

  bool ApplyPatch(XexModule* module);
  bool Load(const std::string& name, const std::string& path,
            const void* xex_addr, size_t xex_length);
  bool Unload();

  const std::string& name() const override { return name_; }
  bool is_executable() const override {
    return (xex_header()->module_flags & XEX_MODULE_TITLE) != 0;
  }

  bool ContainsAddress(uint32_t address) override;

  static void DecryptBuffer(const uint8_t* session_key,
                            const uint8_t* input_buffer,
                            const size_t input_size, uint8_t* output_buffer,
                            const size_t output_size);

  uint8_t* HostData() {
    if (base_address_)
      return memory()->TranslateVirtual(base_address_);
    else
      return nullptr;
  }

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override;

 private:
  void DecryptSessionKey(bool useDevkit = false);

  int ReadImage(const void* xex_addr, size_t xex_length);
  int ReadImageUncompressed(const void* xex_addr, size_t xex_length);
  int ReadImageBasicCompressed(const void* xex_addr, size_t xex_length);
  int ReadImageCompressed(const void* xex_addr, size_t xex_length);

  int ReadPEHeaders();

  bool SetupLibraryImports(const char* name,
                           const xex2_import_library* library);
  bool FindSaveRest();

  Processor* processor_ = nullptr;
  kernel::KernelState* kernel_state_ = nullptr;
  std::string name_;
  std::string path_;
  std::vector<uint8_t> xex_header_mem_;  // Holds the xex header

  // various optional headers
  std::vector<ImportLibrary>
      import_libs_;  // pre-loaded import libraries for ease of use

  std::vector<PESection> pe_sections_;

  uint8_t session_key_[0x10];

  bool loaded_ = false;  // Loaded into memory?

  uint32_t base_address_ = 0;
  uint32_t low_address_ = 0;
  uint32_t high_address_ = 0;

  bool is_dev_kit_ = false;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_XEX_MODULE_H_
