/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb/symbol.h>

#include <xenia/cpu/ppc/instr.h>


using namespace std;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


FunctionBlock::FunctionBlock() :
    start_address(0), end_address(0),
    outgoing_type(kTargetUnknown), outgoing_address(0),
    outgoing_function(0) {
}

FunctionSymbol::FunctionSymbol() :
    Symbol(Function),
    start_address(0), end_address(0), name(0),
    type(Unknown), flags(0),
    kernel_export(0), ee(0) {
}

FunctionSymbol::~FunctionSymbol() {
  delete name;
  for (std::map<uint32_t, FunctionBlock*>::iterator it = blocks.begin();
       it != blocks.end(); ++it) {
    delete it->second;
  }
}

FunctionBlock* FunctionSymbol::GetBlock(uint32_t address) {
  std::map<uint32_t, FunctionBlock*>::iterator it = blocks.find(address);
  if (it != blocks.end()) {
    return it->second;
  }
  return NULL;
}

FunctionBlock* FunctionSymbol::SplitBlock(uint32_t address) {
  // Scan to find the block that contains the address.
  for (std::map<uint32_t, FunctionBlock*>::iterator it = blocks.begin();
       it != blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    if (address == block->start_address) {
      // No need for a split.
      return block;
    } else if (address >= block->start_address &&
               address <= block->end_address + 4) {
      // Inside this block.
      // Since we know we are starting inside of the block we split downwards.
      FunctionBlock* new_block = new FunctionBlock();
      new_block->start_address = address;
      new_block->end_address = block->end_address;
      new_block->outgoing_type = block->outgoing_type;
      new_block->outgoing_address = block->outgoing_address;
      new_block->outgoing_block = block->outgoing_block;
      blocks.insert(std::pair<uint32_t, FunctionBlock*>(address, new_block));
      // Patch up old block.
      block->end_address = address - 4;
      block->outgoing_type = FunctionBlock::kTargetNone;
      block->outgoing_address = 0;
      block->outgoing_block = NULL;
      return new_block;
    }
  }
  return NULL;
}

VariableSymbol::VariableSymbol() :
    Symbol(Variable),
    address(0), name(0),
    kernel_export(0) {
}

VariableSymbol::~VariableSymbol() {
  delete name;
}

ExceptionEntrySymbol::ExceptionEntrySymbol() :
  Symbol(ExceptionEntry),
  address(0), function(0) {
}
