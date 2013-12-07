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

#include <alloy/runtime/module.h>
#include <xenia/core.h>
#include <xenia/kernel/xex2.h>


namespace xe {
namespace cpu {

class XenonRuntime;


class XexModule : public alloy::runtime::Module {
public:
  XexModule(XenonRuntime* runtime);
  virtual ~XexModule();

  int Load(const char* name, const char* path, xe_xex2_ref xex);

  virtual const char* name() const { return name_; }

  virtual bool ContainsAddress(uint64_t address);

private:
  int SetupImports(xe_xex2_ref xex);
  int SetupLibraryImports(const xe_xex2_import_library_t* library);
  int FindSaveRest();

private:
  XenonRuntime* runtime_;
  char*       name_;
  char*       path_;
  xe_xex2_ref xex_;

  uint64_t    base_address_;
  uint64_t    low_address_;
  uint64_t    high_address_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_XEX_MODULE_H_
