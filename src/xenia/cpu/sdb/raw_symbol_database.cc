/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb/raw_symbol_database.h>

#include <xenia/cpu/ppc/instr.h>


using namespace std;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


RawSymbolDatabase::RawSymbolDatabase(
    xe_memory_ref memory, ExportResolver* export_resolver,
    SymbolTable* sym_table,
    uint32_t start_address, uint32_t end_address) :
    SymbolDatabase(memory, export_resolver, sym_table) {
  start_address_ = start_address;
  end_address_ = end_address;
}

RawSymbolDatabase::~RawSymbolDatabase() {
}

uint32_t RawSymbolDatabase::GetEntryPoint() {
  return start_address_;
}

bool RawSymbolDatabase::IsValueInTextRange(uint32_t value) {
  return value >= start_address_ && value < end_address_;
}

