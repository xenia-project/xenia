/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_USERMODULE_H_
#define XENIA_CPU_USERMODULE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/cpu/sdb.h>
#include <xenia/kernel/xex2.h>


namespace xe {
namespace cpu {


class ExecModule {
public:
  ExecModule(
      xe_memory_ref memory, ExportResolver* export_resolver,
      sdb::SymbolTable* sym_table,
      const char* module_name, const char* module_path);
  ~ExecModule();

  sdb::SymbolDatabase* sdb();

  int PrepareRawBinary(uint32_t start_address, uint32_t end_address);
  int PrepareXexModule(xe_xex2_ref xex);

  sdb::FunctionSymbol* FindFunctionSymbol(uint32_t address);

  void Dump();

private:
  int Prepare();
  int Init();

  xe_memory_ref       memory_;
  ExportResolver*     export_resolver_;
  sdb::SymbolTable*   sym_table_;
  char*               module_name_;
  char*               module_path_;

  shared_ptr<sdb::SymbolDatabase>     sdb_;
  uint32_t    code_addr_low_;
  uint32_t    code_addr_high_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_USERMODULE_H_
