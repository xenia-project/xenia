/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_XEX_SYMBOL_DATABASE_H_
#define XENIA_CPU_SDB_XEX_SYMBOL_DATABASE_H_

#include <xenia/cpu/sdb/symbol_database.h>

#include <xenia/kernel/xex2.h>


namespace xe {
namespace cpu {
namespace sdb {


class XexSymbolDatabase : public SymbolDatabase {
public:
  XexSymbolDatabase(xe_memory_ref memory,
                    kernel::ExportResolver* export_resolver,
                    SymbolTable* sym_table, xe_xex2_ref xex);
  virtual ~XexSymbolDatabase();

  virtual int Analyze();

private:
  int FindSaveRest();
  int AddImports(const xe_xex2_import_library_t *library);
  int AddMethodHints();

  virtual uint32_t GetEntryPoint();
  virtual bool IsValueInTextRange(uint32_t value);

  xe_xex2_ref xex_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_XEX_SYMBOL_DATABASE_H_
