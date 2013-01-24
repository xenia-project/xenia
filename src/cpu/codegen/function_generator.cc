/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/codegen/function_generator.h>

#include <xenia/cpu/ppc/state.h>

#include "cpu/cpu-private.h"


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


FunctionGenerator::FunctionGenerator(
    xe_memory_ref memory, SymbolDatabase* sdb, FunctionSymbol* fn,
    LLVMContext* context, Module* gen_module, Function* gen_fn) {
  memory_ = memory;
  sdb_ = sdb;
  fn_ = fn;
  context_ = context;
  gen_module_ = gen_module;
  gen_fn_ = gen_fn;
  builder_ = new IRBuilder<>(*context_);

  xe_zero_struct(&values_, sizeof(values_));
}

FunctionGenerator::~FunctionGenerator() {
  delete builder_;
}

SymbolDatabase* FunctionGenerator::sdb() {
  return sdb_;
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

FunctionBlock* FunctionGenerator::fn_block() {
  return fn_block_;
}

void FunctionGenerator::GenerateBasicBlocks() {
  // Always add an entry block.
  BasicBlock* entry = BasicBlock::Create(*context_, "entry", gen_fn_);
  builder_->SetInsertPoint(entry);

  if (FLAGS_trace_user_calls) {
    Value* traceUserCall = gen_module_->getGlobalVariable("XeTraceUserCall");
    builder_->CreateCall3(
        traceUserCall,
        gen_fn_->arg_begin(),
        builder_->getInt32(fn_->start_address),
        builder_->getInt32(0));
  }

  // If this function is empty, abort!
  if (!fn_->blocks.size()) {
    builder_->CreateRetVoid();
    return;
  }

  // Pass 1 creates all of the blocks - this way we can branch to them.
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
       it != fn_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;

    char name[32];
    xesnprintfa(name, XECOUNT(name), "loc_%.8X", block->start_address);
    BasicBlock* bb = BasicBlock::Create(*context_, name, gen_fn_);
    bbs_.insert(std::pair<uint32_t, BasicBlock*>(block->start_address, bb));
  }

  // Entry always jumps to the first bb.
  builder_->SetInsertPoint(entry);
  builder_->CreateBr(bbs_.begin()->second);

  // Pass 2 fills in instructions.
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
       it != fn_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    GenerateBasicBlock(block, GetBasicBlock(block->start_address));
  }
}

void FunctionGenerator::GenerateBasicBlock(FunctionBlock* block,
                                           BasicBlock* bb) {
  printf("  bb %.8X-%.8X:\n", block->start_address, block->end_address);

  fn_block_ = block;
  bb_ = bb;

  // Move the builder to this block and setup.
  builder_->SetInsertPoint(bb);
  //i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));

  Value* traceInstruction =
      gen_module_->getGlobalVariable("XeTraceInstruction");

  // Walk instructions in block.
  uint8_t* p = xe_memory_addr(memory_, 0);
  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
    InstrData i;
    i.address = ia;
    i.code = XEGETUINT32BE(p + ia);
    i.type = ppc::GetInstrType(i.code);

    if (FLAGS_trace_instructions) {
      builder_->CreateCall3(
          traceInstruction,
          gen_fn_->arg_begin(),
          builder_->getInt32(i.address),
          builder_->getInt32(i.code));
    }

    if (!i.type) {
      XELOGCPU("Invalid instruction at %.8X: %.8X\n", ia, i.code);
      continue;
    }
    printf("    %.8X: %.8X %s\n", ia, i.code, i.type->name);

    // TODO(benvanik): debugging information? source/etc?
    // builder_>SetCurrentDebugLocation(DebugLoc::get(
    //     ia >> 8, ia & 0xFF, ctx->cu));

    typedef int (*InstrEmitter)(FunctionGenerator& g, IRBuilder<>& b,
                                InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;
    XEASSERTNOTNULL(emit);
    int result = emit(*this, *builder_, i);
    if (result) {
      printf("---- error generating instruction: %.8X\n", ia);
    }
  }

  // If we fall through, create the branch.
  if (block->outgoing_type == FunctionBlock::kTargetNone) {
    // Flush registers.
    // TODO(benvanik): only do this before jumps out.
    FlushRegisters();

    BasicBlock* next_bb = GetNextBasicBlock();
    XEASSERTNOTNULL(next_bb);
    builder_->CreateBr(next_bb);
  } else if (block->outgoing_type == FunctionBlock::kTargetUnknown) {
    // Hrm.
    // TODO(benvanik): assert this doesn't occur - means a bad sdb run!
    XELOGCPU("SDB function scan error in %.8X: bb %.8X has unknown exit\n",
        fn_->start_address, block->start_address);
    builder_->CreateRetVoid();
  }

  // TODO(benvanik): finish up BB
}

BasicBlock* FunctionGenerator::GetBasicBlock(uint32_t address) {
  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(address);
  if (it != bbs_.end()) {
    return it->second;
  }
  return NULL;
}

BasicBlock* FunctionGenerator::GetNextBasicBlock() {
  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(
      fn_block_->start_address);
  ++it;
  if (it != bbs_.end()) {
    return it->second;
  }
  return NULL;
}

Function* FunctionGenerator::GetFunction(FunctionSymbol* fn) {
  Function* result = gen_module_->getFunction(StringRef(fn->name));
  if (!result) {
    XELOGE("Static function not found: %.8X %s\n", fn->start_address, fn->name);
  }
  XEASSERTNOTNULL(result);
  return result;
}

Value* FunctionGenerator::LoadStateValue(uint32_t offset, Type* type,
                                         const char* name) {
  PointerType* pointerTy = PointerType::getUnqual(type);
  Function::arg_iterator args = gen_fn_->arg_begin();
  Value* statePtr = args;
  Value* address = builder_->CreateConstInBoundsGEP1_64(
      statePtr, offset);
  Value* ptr = builder_->CreatePointerCast(address, pointerTy);
  return builder_->CreateLoad(ptr, name);
}

void FunctionGenerator::StoreStateValue(uint32_t offset, Type* type,
                                        Value* value) {
  PointerType* pointerTy = PointerType::getUnqual(type);
  Function::arg_iterator args = gen_fn_->arg_begin();
  Value* statePtr = args;
  Value* address = builder_->CreateConstInBoundsGEP1_64(
      statePtr, offset);
  Value* ptr = builder_->CreatePointerCast(address, pointerTy);

  // Widen to target type if needed.
  if (!value->getType()->isIntegerTy(type->getIntegerBitWidth())) {
    value = builder_->CreateZExt(value, type);
  }

  builder_->CreateStore(value, ptr);
}

Value* FunctionGenerator::cia_value() {
  return builder_->getInt32(cia_);
}

void FunctionGenerator::FlushRegisters() {
  // This flushes all local registers (if written) to the register bank and
  // resets their values.
  //
  // TODO(benvanik): only flush if actually required, or selective flushes.

  // xer

  if (values_.lr && values_.lr_dirty) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, lr),
        builder_->getInt64Ty(),
        values_.lr);
    values_.lr = NULL;
    values_.lr_dirty = false;
  }

  if (values_.ctr && values_.ctr_dirty) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, ctr),
        builder_->getInt64Ty(),
        values_.ctr);
    values_.ctr = NULL;
    values_.ctr_dirty = false;
  }

  // TODO(benvanik): don't flush across calls?
  if (values_.cr && values_.cr_dirty) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, cr),
        builder_->getInt64Ty(),
        values_.cr);
    values_.cr = NULL;
    values_.cr_dirty = false;
  }

  for (uint32_t n = 0; n < XECOUNT(values_.gpr); n++) {
    Value* v = values_.gpr[n];
    if (v && (values_.gpr_dirty_bits & (1 << n))) {
      StoreStateValue(
          offsetof(xe_ppc_state_t, r) + 8 * n,
          builder_->getInt64Ty(),
          values_.gpr[n]);
      values_.gpr[n] = NULL;
    }
  }
  values_.gpr_dirty_bits = 0;
}

Value* FunctionGenerator::xer_value() {
  if (true) {//!values_.xer) {
    // Fetch from register bank.
    Value* v = LoadStateValue(
        offsetof(xe_ppc_state_t, xer),
        builder_->getInt64Ty(),
        "xer_");
    values_.xer = v;
    return v;
  } else {
    // Return local.
    return values_.xer;
  }
}

void FunctionGenerator::update_xer_value(Value* value) {
  // Widen to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }

  values_.xer = value;
  values_.xer_dirty = true;
}

Value* FunctionGenerator::lr_value() {
  if (true) {//!values_.lr) {
    // Fetch from register bank.
    Value* v = LoadStateValue(
        offsetof(xe_ppc_state_t, lr),
        builder_->getInt64Ty(),
        "lr_");
    values_.lr = v;
    return v;
  } else {
    // Return local.
    return values_.lr;
  }
}

void FunctionGenerator::update_lr_value(Value* value) {
  // Widen to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }

  values_.lr = value;
  values_.lr_dirty = true;
}

Value* FunctionGenerator::ctr_value() {
  if (true) {//!values_.ctr) {
    // Fetch from register bank.
    Value* v = LoadStateValue(
        offsetof(xe_ppc_state_t, ctr),
        builder_->getInt64Ty(),
        "ctr_");
    values_.ctr = v;
    return v;
  } else {
    // Return local.
    return values_.ctr;
  }
}

void FunctionGenerator::update_ctr_value(Value* value) {
  // Widen to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }

  values_.ctr = value;
  values_.ctr_dirty = true;
}

Value* FunctionGenerator::cr_value() {
  if (true) {//!values_.cr) {
    // Fetch from register bank.
    Value* v = LoadStateValue(
        offsetof(xe_ppc_state_t, cr),
        builder_->getInt64Ty(),
        "cr_");
    values_.cr = v;
    return v;
  } else {
    // Return local.
    return values_.cr;
  }
}

void FunctionGenerator::update_cr_value(Value* value) {
  values_.cr = value;
  values_.cr_dirty = true;
}

Value* FunctionGenerator::gpr_value(uint32_t n) {
  if (n == 0) {
    // Always force zero to a constant - this should help LLVM.
    return builder_->getInt64(0);
  }
  if (true) {//!values_.gpr[n]) {
    // Need to fetch from register bank.
    char name[30];
    xesnprintfa(name, XECOUNT(name), "gpr_r%d_", n);
    Value* v = LoadStateValue(
        offsetof(xe_ppc_state_t, r) + 8 * n,
        builder_->getInt64Ty(),
        name);
    values_.gpr[n] = v;
    return v;
  } else {
    // Local value, reuse.
    return values_.gpr[n];
  }
}

void FunctionGenerator::update_gpr_value(uint32_t n, Value* value) {
  if (n == 0) {
    // Ignore writes to zero.
    return;
  }

  // Widen to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }

  values_.gpr[n] = value;
  values_.gpr_dirty_bits |= 1 << n;
}

Value* FunctionGenerator::GetMembase() {
  Value* v = gen_module_->getGlobalVariable("xe_memory_base");
  return builder_->CreateLoad(v);
}

Value* FunctionGenerator::memory_addr(uint32_t addr) {
  return NULL;
}

Value* FunctionGenerator::ReadMemory(Value* addr, uint32_t size, bool extend) {
  Type* dataTy = NULL;
  switch (size) {
    case 1:
      dataTy = builder_->getInt8Ty();
      break;
    case 2:
      dataTy = builder_->getInt16Ty();
      break;
    case 4:
      dataTy = builder_->getInt32Ty();
      break;
    case 8:
      dataTy = builder_->getInt64Ty();
      break;
    default:
      XEASSERTALWAYS();
      return NULL;
  }
  PointerType* pointerTy = PointerType::getUnqual(dataTy);

  Value* address = builder_->CreateInBoundsGEP(GetMembase(), addr);
  Value* ptr = builder_->CreatePointerCast(address, pointerTy);
  return builder_->CreateLoad(ptr);
}

void FunctionGenerator::WriteMemory(Value* addr, uint32_t size, Value* value) {
  Type* dataTy = NULL;
  switch (size) {
    case 1:
      dataTy = builder_->getInt8Ty();
      break;
    case 2:
      dataTy = builder_->getInt16Ty();
      break;
    case 4:
      dataTy = builder_->getInt32Ty();
      break;
    case 8:
      dataTy = builder_->getInt64Ty();
      break;
    default:
      XEASSERTALWAYS();
      return;
  }
  PointerType* pointerTy = PointerType::getUnqual(dataTy);

  Value* address = builder_->CreateInBoundsGEP(GetMembase(), addr);
  Value* ptr = builder_->CreatePointerCast(address, pointerTy);

  // Truncate, if required.
  if (value->getType() != dataTy) {
    value = builder_->CreateTrunc(value, dataTy);
  }
  builder_->CreateStore(value, ptr);
}
