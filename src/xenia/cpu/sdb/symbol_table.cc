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
  lock_ = xe_mutex_alloc(10000);
  XEASSERTNOTNULL(lock_);
}

SymbolTable::~SymbolTable() {
  xe_mutex_free(lock_);
  lock_ = NULL;
}

int SymbolTable::AddFunction(uint32_t address, FunctionSymbol* symbol) {
  xe_mutex_lock(lock_);
  map_[address] = symbol;
  xe_mutex_unlock(lock_);
  return 0;
}

FunctionSymbol* SymbolTable::GetFunction(uint32_t address) {
  xe_mutex_lock(lock_);
  FunctionMap::const_iterator it = map_.find(address);
  FunctionSymbol* result = it != map_.end() ? it->second : NULL;
  xe_mutex_unlock(lock_);
  return result;
}
