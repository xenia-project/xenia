/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/codegen/function_generator.h>


using namespace llvm;
using namespace xe::cpu::codegen;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


/**
 * This generates function code.
 * One context is created for each function to generate. Each basic block in
 * the function is created and stashed in one pass, then filled in the next.
 *
 * This context object is a stateful representation of the current machine state
 * and all accessors to registers should occur through it. By doing so it's
 * possible to exploit the SSA nature of LLVM to reuse register values within
 * a function without needing to flush to memory.
 *
 * Function calls (any branch outside of the function) will result in an
 * expensive flush of registers.
 *
 * TODO(benvanik): track arguments by looking for register reads without writes
 * TODO(benvanik): avoid flushing registers for leaf nodes
 * TODO(benvnaik): pass return value in LLVM return, not by memory
 */


FunctionGenerator::FunctionGenerator(xe_memory_ref memory, FunctionSymbol* fn,
                           LLVMContext* context, Module* gen_module,
                           Function* gen_fn) {
  memory_ = memory;
  fn_ = fn;
  context_ = context;
  gen_module_ = gen_module;
  gen_fn_ = gen_fn;
  builder_ = new IRBuilder<>(*context_);
}

FunctionGenerator::~FunctionGenerator() {
  delete builder_;
}

FunctionSymbol* FunctionGenerator::fn() {
  return fn_;
}

llvm::LLVMContext* FunctionGenerator::context() {
  return context_;
}

llvm::Module* FunctionGenerator::gen_module() {
  return gen_module_;
}

llvm::Function* FunctionGenerator::gen_fn() {
  return gen_fn_;
}

void FunctionGenerator::GenerateBasicBlocks() {
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

void FunctionGenerator::GenerateBasicBlock(FunctionBlock* block,
                                           BasicBlock* bb) {
  printf("  bb %.8X-%.8X:\n", block->start_address, block->end_address);

  // Move the builder to this block and setup.
  builder_->SetInsertPoint(bb);
  //i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));

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

    // TODO(benvanik): debugging information? source/etc?
    // builder_>SetCurrentDebugLocation(DebugLoc::get(
    //     ia >> 8, ia & 0xFF, ctx->cu));

    //emit(this, i, builder);
  }

  //Value* tmp = builder_->getInt32(0);
  builder_->CreateRetVoid();

  // TODO(benvanik): finish up BB
}

BasicBlock* FunctionGenerator::GetBasicBlock(uint32_t address) {
  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(address);
  if (it != bbs_.end()) {
    return it->second;
  }
  return NULL;
}

Function* FunctionGenerator::GetFunction(uint32_t addr) {
  return NULL;
}

Value* FunctionGenerator::cia_value() {
  return builder_->getInt32(cia_);
}

void FunctionGenerator::FlushRegisters() {

}

Value* FunctionGenerator::xer_value() {
  return NULL;
}

void FunctionGenerator::update_xer_value(Value* value) {

}

Value* FunctionGenerator::lr_value() {
  return NULL;
}

void FunctionGenerator::update_lr_value(Value* value) {

}

Value* FunctionGenerator::ctr_value() {
  return NULL;
}

void FunctionGenerator::update_ctr_value(Value* value) {

}

Value* FunctionGenerator::cr_value(uint32_t n) {
  return NULL;
}

void FunctionGenerator::update_cr_value(uint32_t n, Value* value) {
  //
}

Value* FunctionGenerator::gpr_value(uint32_t n) {
  return NULL;
}

void FunctionGenerator::update_gpr_value(uint32_t n, Value* value) {
  //
}

Value* FunctionGenerator::memory_addr(uint32_t addr) {
  return NULL;
}

Value* FunctionGenerator::read_memory(Value* addr, uint32_t size, bool extend) {
  return NULL;
}

void FunctionGenerator::write_memory(Value* addr, uint32_t size, Value* value) {
  //
}
