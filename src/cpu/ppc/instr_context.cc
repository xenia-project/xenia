/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/ppc/instr_context.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>


using namespace llvm;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


InstrContext::InstrContext(xe_memory_ref memory, FunctionSymbol* fn,
                           LLVMContext* context, Module* gen_module,
                           Function* gen_fn) {
  memory_ = memory;
  fn_ = fn;
  context_ = context;
  gen_module_ = gen_module;
  gen_fn_ = gen_fn;
}

InstrContext::~InstrContext() {
}

FunctionSymbol* InstrContext::fn() {
  return fn_;
}

llvm::LLVMContext* InstrContext::context() {
  return context_;
}

llvm::Module* InstrContext::gen_module() {
  return gen_module_;
}

llvm::Function* InstrContext::gen_fn() {
  return gen_fn_;
}

void InstrContext::GenerateBasicBlocks() {
  // Pass 1 creates all of the blocks - this way we can branch to them.
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
       it != fn_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;

    char name[32];
    xesnprintfa(name, XECOUNT(name), "loc_%.8X", block->start_address);
    BasicBlock* bb = BasicBlock::Create(*context_, name, gen_fn_);
    bbs_.insert(std::pair<uint32_t, BasicBlock*>(block->start_address, bb));
  }

  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
       it != fn_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    GenerateBasicBlock(block, GetBasicBlock(block->start_address));
  }
}

void InstrContext::GenerateBasicBlock(FunctionBlock* block, BasicBlock* bb) {
  printf("  bb %.8X-%.8X:\n", block->start_address, block->end_address);

  //builder.SetCurrentDebugLocation(DebugLoc::get(fn->start_address >> 8, fn->start_address & 0xFF, ctx->cu));
  //i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));

  IRBuilder<> builder(bb);

  // Walk instructions in block.
  uint8_t* p = xe_memory_addr(memory_, 0);
  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
    InstrData i;
    i.address = ia;
    i.code = XEGETUINT32BE(p + ia);
    i.type = ppc::GetInstrType(i.code);
    if (!i.type) {
      XELOGCPU("Invalid instruction at %.8X: %.8X\n", ia, i.code);
      continue;
    }
    printf("    %.8X: %.8X %s\n", ia, i.code, i.type->name);
    // stash current bb?
    // stash builder
    //emit(this, i);
  }

  Value* tmp = builder.getInt32(0);
  builder.CreateRet(tmp);

  // TODO(benvanik): finish up BB
}

BasicBlock* InstrContext::GetBasicBlock(uint32_t address) {
  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(address);
  if (it != bbs_.end()) {
    return it->second;
  }
  return NULL;
}

Function* InstrContext::GetFunction(uint32_t addr) {
  return NULL;
}

Value* InstrContext::cia_value() {
  return NULL;
}

Value* InstrContext::nia_value() {
  return NULL;
}

void InstrContext::update_nia_value(Value* value) {
  //
}

Value* InstrContext::spr_value(uint32_t n) {
  return NULL;
}

void InstrContext::update_spr_value(uint32_t n, Value* value) {
  //
}

Value* InstrContext::cr_value() {
  return NULL;
}

void InstrContext::update_cr_value(Value* value) {
  //
}

Value* InstrContext::gpr_value(uint32_t n) {
  return NULL;
}

void InstrContext::update_gpr_value(uint32_t n, Value* value) {
  //
}

Value* InstrContext::memory_addr(uint32_t addr) {
  return NULL;
}

Value* InstrContext::read_memory(Value* addr, uint32_t size, bool extend) {
  return NULL;
}

void InstrContext::write_memory(Value* addr, uint32_t size, Value* value) {
  //
}
