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
  fn_block_ = NULL;
  return_block_ = NULL;
  internal_indirection_block_ = NULL;
  external_indirection_block_ = NULL;
  bb_ = NULL;

  locals_.indirection_target = NULL;
  locals_.indirection_cia = NULL;

  locals_.xer = NULL;
  locals_.lr = NULL;
  locals_.ctr = NULL;
  locals_.cr = NULL;
  for (size_t n = 0; n < XECOUNT(locals_.gpr); n++) {
    locals_.gpr[n] = NULL;
  }
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
    SpillRegisters();
    Value* traceUserCall = gen_module_->getGlobalVariable("XeTraceUserCall");
    builder_->CreateCall3(
        traceUserCall,
        gen_fn_->arg_begin(),
        builder_->getInt64(fn_->start_address),
        ++gen_fn_->arg_begin());
  }

  // If this function is empty, abort!
  if (!fn_->blocks.size()) {
    builder_->CreateRetVoid();
    return;
  }

  // Create a return block.
  // This spills registers and returns. All non-tail returns should branch
  // here to do the return and ensure registers are spilled.
  return_block_ = BasicBlock::Create(*context_, "return", gen_fn_);

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

  // Setup the shared return/indirection/etc blocks now that we know all the
  // blocks we need and all the registers used.
  GenerateSharedBlocks();
}

void FunctionGenerator::GenerateSharedBlocks() {
  IRBuilder<>& b = *builder_;

  Value* indirect_branch = gen_module_->getGlobalVariable("XeIndirectBranch");

  // Setup the spill block in return.
  b.SetInsertPoint(return_block_);
  SpillRegisters();
  b.CreateRetVoid();

  // Build indirection block on demand.
  // We have already prepped all basic blocks, so we can build these tables now.
  if (external_indirection_block_) {
    // This will spill registers and call the external function.
    // It is only meant for LK=0.
    b.SetInsertPoint(external_indirection_block_);
    SpillRegisters();
    b.CreateCall3(indirect_branch,
                  gen_fn_->arg_begin(),
                  b.CreateLoad(locals_.indirection_target),
                  b.CreateLoad(locals_.indirection_cia));
    b.CreateRetVoid();
  }

  if (internal_indirection_block_) {
    // This will not spill registers and instead try to switch on local blocks.
    // If it fails then the external indirection path is taken.
    // NOTE: we only generate this if a likely local branch is taken.
    b.SetInsertPoint(internal_indirection_block_);
    SwitchInst* switch_i = b.CreateSwitch(
        b.CreateLoad(locals_.indirection_target),
        external_indirection_block_,
        bbs_.size());
    for (std::map<uint32_t, BasicBlock*>::iterator it = bbs_.begin();
         it != bbs_.end(); ++it) {
      switch_i->addCase(b.getInt64(it->first), it->second);
    }
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
      SpillRegisters();
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

BasicBlock* FunctionGenerator::GetReturnBasicBlock() {
  return return_block_;
}

Function* FunctionGenerator::GetFunction(FunctionSymbol* fn) {
  Function* result = gen_module_->getFunction(StringRef(fn->name));
  if (!result) {
    XELOGE("Static function not found: %.8X %s\n", fn->start_address, fn->name);
  }
  XEASSERTNOTNULL(result);
  return result;
}

int FunctionGenerator::GenerateIndirectionBranch(uint32_t cia, Value* target,
                                                 bool lk, bool likely_local) {
  // This function is called by the control emitters when they know that an
  // indirect branch is required.
  // It first tries to see if the branch is to an address within the function
  // and, if so, uses a local switch table. If that fails because we don't know
  // the block the function is regenerated (ACK!). If the target is external
  // then an external call occurs.

  IRBuilder<>& b = *builder_;
  BasicBlock* next_block = GetNextBasicBlock();

  BasicBlock* insert_bb = b.GetInsertBlock();
  BasicBlock::iterator insert_bbi = b.GetInsertPoint();

  // Request builds of the indirection blocks on demand.
  // We can't build here because we don't know what registers will be needed
  // yet, so we just create the blocks and let GenerateSharedBlocks handle it
  // after we are done with all user instructions.
  if (!external_indirection_block_) {
    // Setup locals in the entry block.
    builder_->SetInsertPoint(&gen_fn_->getEntryBlock(),
                             gen_fn_->getEntryBlock().begin());
    locals_.indirection_target = b.CreateAlloca(
        b.getInt64Ty(), 0, "indirection_target");
    locals_.indirection_cia = b.CreateAlloca(
        b.getInt64Ty(), 0, "indirection_cia");

    external_indirection_block_ = BasicBlock::Create(
        *context_, "external_indirection_block", gen_fn_, return_block_);
  }
  if (likely_local && !internal_indirection_block_) {
    internal_indirection_block_ = BasicBlock::Create(
        *context_, "internal_indirection_block", gen_fn_, return_block_);
  }

  b.SetInsertPoint(insert_bb, insert_bbi);

  // Check to see if the target address is within the function.
  // If it is jump to that basic block. If the basic block is not found it means
  // we have a jump inside the function that wasn't identified via static
  // analysis. These are bad as they require function regeneration.
  if (likely_local) {
    // Note that we only support LK=0, as we are using shared tables.
    XEASSERT(!lk);
    b.CreateStore(target, locals_.indirection_target);
    b.CreateStore(b.getInt64(cia), locals_.indirection_cia);
    Value* fn_ge_cmp = b.CreateICmpUGE(target, b.getInt64(fn_->start_address));
    Value* fn_l_cmp = b.CreateICmpULT(target, b.getInt64(fn_->end_address));
    Value* fn_target_cmp = b.CreateAnd(fn_ge_cmp, fn_l_cmp);
    b.CreateCondBr(fn_target_cmp,
                   internal_indirection_block_, external_indirection_block_);
    return 0;
  }

  // If we are LK=0 jump to the shared indirection block. This prevents us
  // from needing to fill the registers again after the call and shares more
  // code.
  if (!lk) {
    b.CreateStore(target, locals_.indirection_target);
    b.CreateStore(b.getInt64(cia), locals_.indirection_cia);
    b.CreateBr(external_indirection_block_);
  } else {
    // Slowest path - spill, call the external function, and fill.
    // We should avoid this at all costs.

    // Spill registers. We could probably share this.
    SpillRegisters();

    // TODO(benvanik): keep function pointer lookup local.
    Value* indirect_branch = gen_module_->getGlobalVariable("XeIndirectBranch");
    b.CreateCall3(indirect_branch,
                  gen_fn_->arg_begin(),
                  target,
                  b.getInt64(cia));

    if (next_block) {
      // Only refill if not a tail call.
      FillRegisters();
      b.CreateBr(next_block);
    } else {
      b.CreateRetVoid();
    }
  }

  return 0;
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
  builder_->CreateStore(value, ptr);
}

Value* FunctionGenerator::cia_value() {
  return builder_->getInt32(cia_);
}

Value* FunctionGenerator::SetupRegisterLocal(uint32_t offset, llvm::Type* type,
                                             const char* name) {
  // Insert into the entry block.
  BasicBlock* insert_bb = builder_->GetInsertBlock();
  BasicBlock::iterator insert_bbi = builder_->GetInsertPoint();
  builder_->SetInsertPoint(&gen_fn_->getEntryBlock(),
                           gen_fn_->getEntryBlock().begin());

  Value* v = builder_->CreateAlloca(type, 0, name);
  builder_->CreateStore(LoadStateValue(offset, type), v);

  builder_->SetInsertPoint(insert_bb, insert_bbi);
  return v;
}

void FunctionGenerator::FillRegisters() {
  // This updates all of the local register values from the state memory.
  // It should be called on function entry for initial setup and after any
  // calls that may modify the registers.

  if (locals_.xer) {
    builder_->CreateStore(LoadStateValue(
        offsetof(xe_ppc_state_t, xer),
        builder_->getInt64Ty()), locals_.xer);
  }

  if (locals_.lr) {
    builder_->CreateStore(LoadStateValue(
        offsetof(xe_ppc_state_t, lr),
        builder_->getInt64Ty()), locals_.lr);
  }

  if (locals_.ctr) {
    builder_->CreateStore(LoadStateValue(
        offsetof(xe_ppc_state_t, ctr),
        builder_->getInt64Ty()), locals_.ctr);
  }

  if (locals_.cr) {
    builder_->CreateStore(LoadStateValue(
        offsetof(xe_ppc_state_t, cr),
        builder_->getInt64Ty()), locals_.cr);
  }

  // Note that we skip zero.
  for (uint32_t n = 1; n < XECOUNT(locals_.gpr); n++) {
    if (locals_.gpr[n]) {
      builder_->CreateStore(LoadStateValue(
          offsetof(xe_ppc_state_t, r) + 8 * n,
          builder_->getInt64Ty()), locals_.gpr[n]);
    }
  }
}

void FunctionGenerator::SpillRegisters() {
  // This flushes all local registers (if written) to the register bank and
  // resets their values.
  //
  // TODO(benvanik): only flush if actually required, or selective flushes.

  if (locals_.xer) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, xer),
        builder_->getInt64Ty(),
        builder_->CreateLoad(locals_.xer));
  }

  if (locals_.lr) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, lr),
        builder_->getInt64Ty(),
        builder_->CreateLoad(locals_.lr));
  }

  if (locals_.ctr) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, ctr),
        builder_->getInt64Ty(),
        builder_->CreateLoad(locals_.ctr));
  }

  // TODO(benvanik): don't flush across calls?
  if (locals_.cr) {
    StoreStateValue(
        offsetof(xe_ppc_state_t, cr),
        builder_->getInt64Ty(),
        builder_->CreateLoad(locals_.cr));
  }

  // Note that we skip zero.
  for (uint32_t n = 1; n < XECOUNT(locals_.gpr); n++) {
    Value* v = locals_.gpr[n];
    if (v) {
      StoreStateValue(
          offsetof(xe_ppc_state_t, r) + 8 * n,
          builder_->getInt64Ty(),
          builder_->CreateLoad(locals_.gpr[n]));
    }
  }
}

Value* FunctionGenerator::xer_value() {
  if (!locals_.xer) {
    locals_.xer = SetupRegisterLocal(
        offsetof(xe_ppc_state_t, xer),
        builder_->getInt64Ty(),
        "xer");
  }
  return locals_.xer;
}

void FunctionGenerator::update_xer_value(Value* value) {
  // Ensure the register is local.
  xer_value();

  // Extend to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }
  builder_->CreateStore(value, locals_.xer);
}

Value* FunctionGenerator::lr_value() {
  if (!locals_.lr) {
    locals_.lr = SetupRegisterLocal(
        offsetof(xe_ppc_state_t, lr),
        builder_->getInt64Ty(),
        "lr");
  }
  return builder_->CreateLoad(locals_.lr);
}

void FunctionGenerator::update_lr_value(Value* value) {
  // Ensure the register is local.
  lr_value();

  // Extend to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }
  builder_->CreateStore(value, locals_.lr);
}

Value* FunctionGenerator::ctr_value() {
  if (!locals_.ctr) {
    locals_.ctr = SetupRegisterLocal(
        offsetof(xe_ppc_state_t, ctr),
        builder_->getInt64Ty(),
        "ctr");
  }
  return builder_->CreateLoad(locals_.ctr);
}

void FunctionGenerator::update_ctr_value(Value* value) {
  // Ensure the register is local.
  ctr_value();

  // Extend to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }
  builder_->CreateStore(value, locals_.ctr);
}

Value* FunctionGenerator::cr_value() {
  if (!locals_.cr) {
    locals_.cr = SetupRegisterLocal(
        offsetof(xe_ppc_state_t, cr),
        builder_->getInt64Ty(),
        "cr");
  }
  return builder_->CreateLoad(locals_.cr);
}

void FunctionGenerator::update_cr_value(Value* value) {
  // Ensure the register is local.
  cr_value();

  // Extend to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }
  builder_->CreateStore(value, locals_.cr);
}

Value* FunctionGenerator::gpr_value(uint32_t n) {
  XEASSERT(n >= 0 && n < 32);
  if (n == 0) {
    // Always force zero to a constant - this should help LLVM.
    return builder_->getInt64(0);
  }
  if (!locals_.gpr[n]) {
    char name[30];
    xesnprintfa(name, XECOUNT(name), "gpr_r%d", n);
    locals_.gpr[n] = SetupRegisterLocal(
        offsetof(xe_ppc_state_t, r) + 8 * n,
        builder_->getInt64Ty(),
        name);
  }
  return builder_->CreateLoad(locals_.gpr[n]);
}

void FunctionGenerator::update_gpr_value(uint32_t n, Value* value) {
  XEASSERT(n >= 0 && n < 32);

  if (n == 0) {
    // Ignore writes to zero.
    return;
  }

  // Ensure the register is local.
  gpr_value(n);

  // Extend to 64bits if needed.
  if (!value->getType()->isIntegerTy(64)) {
    value = builder_->CreateZExt(value, builder_->getInt64Ty());
  }

  builder_->CreateStore(value, locals_.gpr[n]);
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
