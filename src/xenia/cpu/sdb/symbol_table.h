/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_SYMBOL_TABLE_H_
#define XENIA_CPU_SDB_SYMBOL_TABLE_H_

#include <xenia/core.h>

#include <xenia/cpu/sdb/symbol.h>


namespace xe {
namespace cpu {
namespace sdb {


class SymbolTable {
public:
  SymbolTable();
  ~SymbolTable();

  int AddFunction(uint32_t address, sdb::FunctionSymbol* symbol);
  sdb::FunctionSymbol* GetFunction(uint32_t address);

private:
  typedef std::tr1::unordered_map<uint32_t, sdb::FunctionSymbol*> FunctionMap;
  FunctionMap map_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_SYMBOL_TABLE_H_
