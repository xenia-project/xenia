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
#include "xenia/common.h"
#include "xenia/kernel/util/xex2.h"

namespace xe {
namespace cpu {

class Runtime;

class XexModule : public xe::cpu::Module {
public:
 XexModule(Runtime* runtime);
  virtual ~XexModule();

  xe_xex2_ref xex() const { return xex_; }

  int Load(const std::string& name, const std::string& path, xe_xex2_ref xex);

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint64_t address) override;

private:
  int SetupImports(xe_xex2_ref xex);
  int SetupLibraryImports(const xe_xex2_import_library_t* library);
  int FindSaveRest();

private:
 Runtime* runtime_;
  std::string   name_;
  std::string   path_;
  xe_xex2_ref   xex_;

  uint64_t      base_address_;
  uint64_t      low_address_;
  uint64_t      high_address_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_XEX_MODULE_H_
