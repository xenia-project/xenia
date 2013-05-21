/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_RAW_SYMBOL_DATABASE_H_
#define XENIA_CPU_SDB_RAW_SYMBOL_DATABASE_H_

#include <xenia/cpu/sdb/symbol_database.h>


namespace xe {
namespace cpu {
namespace sdb {


class RawSymbolDatabase : public SymbolDatabase {
public:
  RawSymbolDatabase(xe_memory_ref memory,
                    kernel::ExportResolver* export_resolver,
                    SymbolTable* sym_table,
                    uint32_t start_address, uint32_t end_address);
  virtual ~RawSymbolDatabase();

private:
  virtual uint32_t GetEntryPoint();
  virtual bool IsValueInTextRange(uint32_t value);

  uint32_t start_address_;
  uint32_t end_address_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_RAW_SYMBOL_DATABASE_H_
