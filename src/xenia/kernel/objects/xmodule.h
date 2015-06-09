/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XMODULE_H_
#define XENIA_KERNEL_XBOXKRNL_XMODULE_H_

#include <string>

#include "xenia/cpu/module.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XModule : public XObject {
 public:
  enum class ModuleType {
    // Matches debugger Module type.
    kKernelModule = 0,
    kUserModule = 1,
  };

  XModule(KernelState* kernel_state, ModuleType module_type,
          const std::string& path);
  virtual ~XModule();

  ModuleType module_type() const { return module_type_; }
  const std::string& path() const { return path_; }
  const std::string& name() const { return name_; }
  bool Matches(const std::string& name) const;

  xe::cpu::Module* processor_module() const { return processor_module_; }

  virtual uint32_t GetProcAddressByOrdinal(uint16_t ordinal) = 0;
  virtual uint32_t GetProcAddressByName(const char* name) = 0;
  virtual X_STATUS GetSection(const char* name, uint32_t* out_section_data,
                              uint32_t* out_section_size);

 protected:
  void OnLoad();

  ModuleType module_type_;
  std::string name_;
  std::string path_;

  xe::cpu::Module* processor_module_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XMODULE_H_
