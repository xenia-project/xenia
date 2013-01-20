/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/ppc/instr_context.h"


using namespace llvm;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


InstrContext::InstrContext(FunctionSymbol* fn, LLVMContext* context,
                           Module* gen_module, Function* gen_fn) {
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

void InstrContext::AddBasicBlock() {
  //
}

void InstrContext::GenerateBasicBlocks() {
  //
}

BasicBlock* InstrContext::GetBasicBlock(uint32_t address) {
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
