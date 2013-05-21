/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb/symbol_table.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;


SymbolTable::SymbolTable() {
}

SymbolTable::~SymbolTable() {
}

int SymbolTable::AddFunction(uint32_t address, FunctionSymbol* symbol) {
  map_[address] = symbol;
  return 0;
}

FunctionSymbol* SymbolTable::GetFunction(uint32_t address) {
  FunctionMap::const_iterator it = map_.find(address);
  return it != map_.end() ? it->second : NULL;
}
