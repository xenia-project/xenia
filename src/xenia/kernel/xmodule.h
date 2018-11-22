/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XMODULE_H_
#define XENIA_KERNEL_XMODULE_H_

#include <string>

#include "xenia/cpu/module.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// https://www.nirsoft.net/kernel_struct/vista/LDR_DATA_TABLE_ENTRY.html
// HMODULE points to this struct!
struct X_LDR_DATA_TABLE_ENTRY {
  X_LIST_ENTRY in_load_order_links;            // 0x0
  X_LIST_ENTRY in_memory_order_links;          // 0x8
  X_LIST_ENTRY in_initialization_order_links;  // 0x10

  xe::be<uint32_t> dll_base;    // 0x18
  xe::be<uint32_t> image_base;  // 0x1C
  xe::be<uint32_t> image_size;  // 0x20

  X_UNICODE_STRING full_dll_name;  // 0x24
  X_UNICODE_STRING base_dll_name;  // 0x2C

  xe::be<uint32_t> flags;              // 0x34
  xe::be<uint32_t> full_image_size;    // 0x38
  xe::be<uint32_t> entry_point;        // 0x3C
  xe::be<uint16_t> load_count;         // 0x40
  xe::be<uint16_t> module_index;       // 0x42
  xe::be<uint32_t> dll_base_original;  // 0x44
  xe::be<uint32_t> checksum;           // 0x48 hijacked to hold kernel handle
  xe::be<uint32_t> load_flags;         // 0x4C
  xe::be<uint32_t> time_date_stamp;    // 0x50
  xe::be<uint32_t> loaded_imports;     // 0x54
  xe::be<uint32_t> xex_header_base;    // 0x58
  // X_ANSI_STRING load_file_name;     // 0x5C
  xe::be<uint32_t> closure_root;      // 0x5C
  xe::be<uint32_t> traversal_parent;  // 0x60
};

class XModule : public XObject {
 public:
  enum class ModuleType {
    // Matches debugger Module type.
    kKernelModule = 0,
    kUserModule = 1,
  };

  static const Type kType = kTypeModule;

  XModule(KernelState* kernel_state, ModuleType module_type);
  virtual ~XModule();

  ModuleType module_type() const { return module_type_; }
  const std::string& path() const { return path_; }
  const std::string& name() const { return name_; }
  bool Matches(const std::string& name) const;

  xe::cpu::Module* processor_module() const { return processor_module_; }
  uint32_t hmodule_ptr() const { return hmodule_ptr_; }

  virtual uint32_t GetProcAddressByOrdinal(uint16_t ordinal) = 0;
  virtual uint32_t GetProcAddressByName(const char* name) = 0;
  virtual X_STATUS GetSection(const char* name, uint32_t* out_section_data,
                              uint32_t* out_section_size);

  static object_ref<XModule> GetFromHModule(KernelState* kernel_state,
                                            void* hmodule);
  static uint32_t GetHandleFromHModule(void* hmodule);

  virtual bool Save(ByteStream* stream) override;
  static object_ref<XModule> Restore(KernelState* kernel_state,
                                     ByteStream* stream);

 protected:
  void OnLoad();
  void OnUnload();
  static std::string NameFromPath(std::string path);

  ModuleType module_type_;
  std::string name_;
  std::string path_;

  xe::cpu::Module* processor_module_;

  uint32_t hmodule_ptr_;  // This points to LDR_DATA_TABLE_ENTRY.
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XMODULE_H_
