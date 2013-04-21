/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/function_table.h>


using namespace xe;
using namespace xe::cpu;


FunctionTable::FunctionTable() {
}

FunctionTable::~FunctionTable() {
}

int FunctionTable::AddCodeRange(uint32_t low_address, uint32_t high_address) {
  return 0;
}

FunctionPointer FunctionTable::BeginAddFunction(uint32_t address) {
  FunctionPointer ptr = map_[address];
  if (ptr) {
    return ptr;
  }
  map_[address] = reinterpret_cast<FunctionPointer>(0x1);
  return NULL;
}

int FunctionTable::AddFunction(uint32_t address, FunctionPointer ptr) {
  map_[address] = ptr;
  return 0;
}

FunctionPointer FunctionTable::GetFunction(uint32_t address) {
  FunctionMap::const_iterator it = map_.find(address);
  return it != map_.end() ? it->second : NULL;
}
