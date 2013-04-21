/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CODEGEN_RECOMPILER_H_
#define XENIA_CPU_CODEGEN_RECOMPILER_H_

#include <xenia/core.h>

#include <xenia/cpu/ppc.h>


namespace xe {
namespace cpu {
namespace codegen {


class Recompiler {
public:
  Recompiler(
      xe_memory_ref memory, shared_ptr<kernel::ExportResolver> export_resolver,
      shared_ptr<sdb::SymbolDatabase> sdb, const char* module_name);
  ~Recompiler();

  int Process();

private:
  int GenerateCodeUnits();
  int LinkCodeUnits();
  int LoadLibrary(const char* path);

  xechar_t* library_path_;
};



}  // namespace codegen
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODEGEN_RECOMPILER_H_
