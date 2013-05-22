/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/libjit/libjit_emitter.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/ppc/state.h>


using namespace xe::cpu::libjit;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;


DEFINE_bool(memory_address_verification, false,
    "Whether to add additional checks to generated memory load/stores.");
DEFINE_bool(log_codegen, false,
    "Log codegen to stdout.");


/**
 * This generates function code.
 * One context is created and shared for each function to generate.
 * Each basic block in the function is created and stashed in one pass, then
 * filled in the next.
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


LibjitEmitter::LibjitEmitter(xe_memory_ref memory, jit_context_t context) {
  memory_ = memory;
  context_ = context;

  // Grab global exports.
  cpu::GetGlobalExports(&global_exports_);

  // Function type for all functions.
  // TODO(benvanik): evaluate using jit_abi_fastcall
  jit_type_t fn_params[] = {
    jit_type_void_ptr,
    jit_type_uint
  };
  fn_signature_ = jit_type_create_signature(
      jit_abi_cdecl,
      jit_type_void,
      fn_params, XECOUNT(fn_params),
      0);

  jit_type_t shim_params[] = {
    jit_type_void_ptr,
    jit_type_void_ptr,
  };
  shim_signature_ = jit_type_create_signature(
      jit_abi_cdecl,
      jit_type_void,
      shim_params, XECOUNT(shim_params),
      0);

  jit_type_t global_export_params_2[] = {
    jit_type_void_ptr,
    jit_type_ulong,
  };
  global_export_signature_2_ = jit_type_create_signature(
      jit_abi_cdecl,
      jit_type_void,
      global_export_params_2, XECOUNT(global_export_params_2),
      0);
  jit_type_t global_export_params_3[] = {
    jit_type_void_ptr,
    jit_type_ulong,
    jit_type_ulong,
  };
  global_export_signature_3_ = jit_type_create_signature(
      jit_abi_cdecl,
      jit_type_void,
      global_export_params_3, XECOUNT(global_export_params_3),
      0);
  jit_type_t global_export_params_4[] = {
    jit_type_void_ptr,
    jit_type_ulong,
    jit_type_ulong,
    jit_type_void_ptr,
  };
  global_export_signature_4_ = jit_type_create_signature(
      jit_abi_cdecl,
      jit_type_void,
      global_export_params_4, XECOUNT(global_export_params_4),
      0);
}

LibjitEmitter::~LibjitEmitter() {
  jit_type_free(fn_signature_);
  jit_type_free(shim_signature_);
  jit_type_free(global_export_signature_2_);
  jit_type_free(global_export_signature_3_);
  jit_type_free(global_export_signature_4_);
}

jit_context_t LibjitEmitter::context() {
  return context_;
}

jit_type_t LibjitEmitter::fn_signature() {
  return fn_signature_;
}

namespace {
int libjit_on_demand_compile(jit_function_t fn) {
  LibjitEmitter* emitter = (LibjitEmitter*)jit_function_get_meta(fn, 0x1000);
  FunctionSymbol* symbol = (FunctionSymbol*)jit_function_get_meta(fn, 0x1001);
  XELOGE("Compile(%s): beginning on-demand compilation...", symbol->name());
  int result_code = emitter->MakeFunction(symbol, fn);
  if (result_code) {
    XELOGCPU("Compile(%s): failed to make function", symbol->name());
    return JIT_RESULT_COMPILE_ERROR;
  }
  return JIT_RESULT_OK;
}
}

int LibjitEmitter::PrepareFunction(FunctionSymbol* symbol) {
  if (symbol->impl_value) {
    return 0;
  }

  jit_context_build_start(context_);

  // Create the function and setup for on-demand compilation.
  jit_function_t fn = jit_function_create(context_, fn_signature_);
  jit_function_set_meta(fn, 0x1000, this, NULL, 0);
  jit_function_set_meta(fn, 0x1001, symbol, NULL, 0);
  jit_function_set_on_demand_compiler(fn, libjit_on_demand_compile);

  // Set optimization options.
  // TODO(benvanik): add gflags
  uint32_t opt_level = 0;
  uint32_t max_level = jit_function_get_max_optimization_level();
  opt_level = MIN(max_level, MAX(0, opt_level));
  jit_function_set_optimization_level(fn, opt_level);

  // Stash for later.
  symbol->impl_value = fn;
  jit_context_build_end(context_);

  return 0;
}

int LibjitEmitter::MakeFunction(FunctionSymbol* symbol, jit_function_t gen_fn) {
  fn_ = symbol;
  gen_fn_ = gen_fn;

  // fn_block_ = NULL;
  // return_block_ = NULL;
  // internal_indirection_block_ = NULL;
  // external_indirection_block_ = NULL;
  // bb_ = NULL;

  // insert_points_.clear();
  // bbs_.clear();

  cia_ = 0;

  access_bits_.Clear();

  locals_.indirection_target = NULL;
  locals_.indirection_cia = NULL;

  locals_.xer = NULL;
  locals_.lr = NULL;
  locals_.ctr = NULL;
  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
    locals_.cr[n] = NULL;
  }
  for (size_t n = 0; n < XECOUNT(locals_.gpr); n++) {
    locals_.gpr[n] = NULL;
  }
  for (size_t n = 0; n < XECOUNT(locals_.fpr); n++) {
    locals_.fpr[n] = NULL;
  }

  if (FLAGS_log_codegen) {
    printf("%s:\n", symbol->name());
  }

  int result_code = 0;
  switch (symbol->type) {
  case FunctionSymbol::User:
    result_code = MakeUserFunction();
    break;
  case FunctionSymbol::Kernel:
    if (symbol->kernel_export && symbol->kernel_export->is_implemented) {
      result_code = MakePresentImportFunction();
    } else {
      result_code = MakeMissingImportFunction();
    }
    break;
  default:
    XEASSERTALWAYS();
    result_code = 1;
    break;
  }

  if (!result_code) {
    // TODO(benvanik): flag
    // pre
    jit_dump_function(stdout, gen_fn_, symbol->name());
    jit_function_compile(gen_fn_);
    // post
    jit_dump_function(stdout, gen_fn_, symbol->name());
  }

  return result_code;
}

int LibjitEmitter::MakeUserFunction() {
  if (FLAGS_trace_user_calls) {
    jit_value_t trace_args[] = {
      jit_value_get_param(gen_fn_, 0),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_->start_address),
      jit_value_get_param(gen_fn_, 1),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_),
    };
    jit_insn_call_native(
        gen_fn_,
        "XeTraceUserCall",
        global_exports_.XeTraceUserCall,
        global_export_signature_4_,
        trace_args, XECOUNT(trace_args),
        0);
  }

  // Emit.
  //emitter_->GenerateBasicBlocks();
  jit_insn_return(gen_fn_, NULL);
  return 0;
}

int LibjitEmitter::MakePresentImportFunction() {
  if (FLAGS_trace_kernel_calls) {
    jit_value_t trace_args[] = {
      jit_value_get_param(gen_fn_, 0),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_->start_address),
      jit_value_get_param(gen_fn_, 1),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_->kernel_export),
    };
    jit_insn_call_native(
        gen_fn_,
        "XeTraceKernelCall",
        global_exports_.XeTraceKernelCall,
        global_export_signature_4_,
        trace_args, XECOUNT(trace_args),
        0);
  }

  // void shim(ppc_state*, shim_data*)
  jit_value_t shim_args[] = {
    jit_value_get_param(gen_fn_, 0),
    jit_value_create_long_constant(gen_fn_, jit_type_ulong,
        (jit_ulong)fn_->kernel_export->function_data.shim_data),
  };
  jit_insn_call_native(
      gen_fn_,
      fn_->kernel_export->name,
      fn_->kernel_export->function_data.shim,
      shim_signature_,
      shim_args, XECOUNT(shim_args),
      0);

  jit_insn_return(gen_fn_, NULL);

  return 0;
}

int LibjitEmitter::MakeMissingImportFunction() {
  if (FLAGS_trace_kernel_calls) {
    jit_value_t trace_args[] = {
      jit_value_get_param(gen_fn_, 0),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_->start_address),
      jit_value_get_param(gen_fn_, 1),
      jit_value_create_long_constant(gen_fn_, jit_type_ulong,
          (jit_ulong)fn_->kernel_export),
    };
    jit_insn_call_native(
        gen_fn_,
        "XeTraceKernelCall",
        global_exports_.XeTraceKernelCall,
        global_export_signature_4_,
        trace_args, XECOUNT(trace_args),
        0);
  }

  jit_insn_return(gen_fn_, NULL);

  return 0;
}

FunctionSymbol* LibjitEmitter::fn() {
  return fn_;
}

jit_function_t LibjitEmitter::gen_fn() {
  return gen_fn_;
}

//FunctionBlock* LibjitEmitter::fn_block() {
//  return fn_block_;
//}
//
//void LibjitEmitter::PushInsertPoint() {
//  IRBuilder<>& b = *builder_;
//  insert_points_.push_back(std::pair<BasicBlock*, BasicBlock::iterator>(
//      b.GetInsertBlock(), b.GetInsertPoint()));
//}
//
//void LibjitEmitter::PopInsertPoint() {
//  IRBuilder<>& b = *builder_;
//  std::pair<BasicBlock*, BasicBlock::iterator> back = insert_points_.back();
//  b.SetInsertPoint(back.first, back.second);
//  insert_points_.pop_back();
//}
//
//void LibjitEmitter::GenerateBasicBlocks() {
//  IRBuilder<>& b = *builder_;
//
//  // Always add an entry block.
//  BasicBlock* entry = BasicBlock::Create(*context_, "entry", gen_fn_);
//  b.SetInsertPoint(entry);
//
//  if (FLAGS_trace_user_calls) {
//    SpillRegisters();
//    Value* traceUserCall = gen_module_->getFunction("XeTraceUserCall");
//    b.CreateCall4(
//        traceUserCall,
//        gen_fn_->arg_begin(),
//        b.getInt64(fn_->start_address),
//        ++gen_fn_->arg_begin(),
//        b.getInt64((uint64_t)fn_));
//  }
//
//  // If this function is empty, abort!
//  if (!fn_->blocks.size()) {
//    b.CreateRetVoid();
//    return;
//  }
//
//  // Create a return block.
//  // This spills registers and returns. All non-tail returns should branch
//  // here to do the return and ensure registers are spilled.
//  return_block_ = BasicBlock::Create(*context_, "return", gen_fn_);
//
//  // Pass 1 creates all of the blocks - this way we can branch to them.
//  // We also track registers used so that when know which ones to fill/spill.
//  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
//       it != fn_->blocks.end(); ++it) {
//    FunctionBlock* block = it->second;
//    XEIGNORE(PrepareBasicBlock(block));
//  }
//
//  // Setup all local variables now that we know what we need.
//  SetupLocals();
//
//  // Pass 2 fills in instructions.
//  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn_->blocks.begin();
//       it != fn_->blocks.end(); ++it) {
//    FunctionBlock* block = it->second;
//    GenerateBasicBlock(block);
//  }
//
//  // Setup the shared return/indirection/etc blocks now that we know all the
//  // blocks we need and all the registers used.
//  GenerateSharedBlocks();
//}
//
//void LibjitEmitter::GenerateSharedBlocks() {
//  IRBuilder<>& b = *builder_;
//
//  Value* indirect_branch = gen_module_->getFunction("XeIndirectBranch");
//
//  // Setup initial register fill in the entry block.
//  // We can only do this once all the locals have been created.
//  b.SetInsertPoint(&gen_fn_->getEntryBlock());
//  FillRegisters();
//  // Entry always falls through to the second block.
//  b.CreateBr(bbs_.begin()->second);
//
//  // Setup the spill block in return.
//  b.SetInsertPoint(return_block_);
//  SpillRegisters();
//  b.CreateRetVoid();
//
//  // Build indirection block on demand.
//  // We have already prepped all basic blocks, so we can build these tables now.
//  if (external_indirection_block_) {
//    // This will spill registers and call the external function.
//    // It is only meant for LK=0.
//    b.SetInsertPoint(external_indirection_block_);
//    SpillRegisters();
//    b.CreateCall3(indirect_branch,
//                  gen_fn_->arg_begin(),
//                  b.CreateLoad(locals_.indirection_target),
//                  b.CreateLoad(locals_.indirection_cia));
//    b.CreateRetVoid();
//  }
//
//  if (internal_indirection_block_) {
//    // This will not spill registers and instead try to switch on local blocks.
//    // If it fails then the external indirection path is taken.
//    // NOTE: we only generate this if a likely local branch is taken.
//    b.SetInsertPoint(internal_indirection_block_);
//    SwitchInst* switch_i = b.CreateSwitch(
//        b.CreateLoad(locals_.indirection_target),
//        external_indirection_block_,
//        static_cast<int>(bbs_.size()));
//    for (std::map<uint32_t, BasicBlock*>::iterator it = bbs_.begin();
//         it != bbs_.end(); ++it) {
//      switch_i->addCase(b.getInt64(it->first), it->second);
//    }
//  }
//}
//
//int LibjitEmitter::PrepareBasicBlock(FunctionBlock* block) {
//  // Create the basic block that will end up getting filled during
//  // generation.
//  char name[32];
//  xesnprintfa(name, XECOUNT(name), "loc_%.8X", block->start_address);
//  BasicBlock* bb = BasicBlock::Create(*context_, name, gen_fn_);
//  bbs_.insert(std::pair<uint32_t, BasicBlock*>(block->start_address, bb));
//
//  // Scan and disassemble each instruction in the block to get accurate
//  // register access bits. In the future we could do other optimization checks
//  // in this pass.
//  // TODO(benvanik): perhaps we want to stash this for each basic block?
//  // We could use this for faster checking of cr/ca checks/etc.
//  InstrAccessBits access_bits;
//  uint8_t* p = xe_memory_addr(memory_, 0);
//  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
//    InstrData i;
//    i.address = ia;
//    i.code = XEGETUINT32BE(p + ia);
//    i.type = ppc::GetInstrType(i.code);
//
//    // Ignore unknown or ones with no disassembler fn.
//    if (!i.type || !i.type->disassemble) {
//      continue;
//    }
//
//    // We really need to know the registers modified, so die if we've been lazy
//    // and haven't implemented the disassemble method yet.
//    ppc::InstrDisasm d;
//    XEASSERTNOTNULL(i.type->disassemble);
//    int result_code = i.type->disassemble(i, d);
//    XEASSERTZERO(result_code);
//    if (result_code) {
//      return result_code;
//    }
//
//    // Accumulate access bits.
//    access_bits.Extend(d.access_bits);
//  }
//
//  // Add in access bits to function access bits.
//  access_bits_.Extend(access_bits);
//
//  return 0;
//}
//
//void LibjitEmitter::GenerateBasicBlock(FunctionBlock* block) {
//  IRBuilder<>& b = *builder_;
//
//  BasicBlock* bb = GetBasicBlock(block->start_address);
//  XEASSERTNOTNULL(bb);
//
//  if (FLAGS_log_codegen) {
//    printf("  bb %.8X-%.8X:\n", block->start_address, block->end_address);
//  }
//
//  fn_block_ = block;
//  bb_ = bb;
//
//  // Move the builder to this block and setup.
//  b.SetInsertPoint(bb);
//  //i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));
//
//  Value* invalidInstruction =
//      gen_module_->getFunction("XeInvalidInstruction");
//  Value* traceInstruction =
//      gen_module_->getFunction("XeTraceInstruction");
//
//  // Walk instructions in block.
//  uint8_t* p = xe_memory_addr(memory_, 0);
//  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
//    InstrData i;
//    i.address = ia;
//    i.code = XEGETUINT32BE(p + ia);
//    i.type = ppc::GetInstrType(i.code);
//
//    if (FLAGS_trace_instructions) {
//      SpillRegisters();
//      b.CreateCall3(
//          traceInstruction,
//          gen_fn_->arg_begin(),
//          b.getInt32(i.address),
//          b.getInt32(i.code));
//    }
//
//    if (!i.type) {
//      XELOGCPU("Invalid instruction %.8X %.8X", ia, i.code);
//      SpillRegisters();
//      b.CreateCall3(
//          invalidInstruction,
//          gen_fn_->arg_begin(),
//          b.getInt32(i.address),
//          b.getInt32(i.code));
//      continue;
//    }
//
//    if (FLAGS_log_codegen) {
//      if (i.type->disassemble) {
//        ppc::InstrDisasm d;
//        i.type->disassemble(i, d);
//        std::string disasm;
//        d.Dump(disasm);
//        printf("    %.8X: %.8X %s\n", ia, i.code, disasm.c_str());
//      } else {
//        printf("    %.8X: %.8X %s ???\n", ia, i.code, i.type->name);
//      }
//    }
//
//    // TODO(benvanik): debugging information? source/etc?
//    // builder_>SetCurrentDebugLocation(DebugLoc::get(
//    //     ia >> 8, ia & 0xFF, ctx->cu));
//
//    typedef int (*InstrEmitter)(LibjitEmitter& g, IRBuilder<>& b,
//                                InstrData& i);
//    InstrEmitter emit = (InstrEmitter)i.type->emit;
//    if (!i.type->emit || emit(*this, *builder_, i)) {
//      // This printf is handy for sort/uniquify to find instructions.
//      //printf("unimplinstr %s\n", i.type->name);
//
//      XELOGCPU("Unimplemented instr %.8X %.8X %s",
//               ia, i.code, i.type->name);
//      SpillRegisters();
//      b.CreateCall3(
//          invalidInstruction,
//          gen_fn_->arg_begin(),
//          b.getInt32(i.address),
//          b.getInt32(i.code));
//    }
//  }
//
//  // If we fall through, create the branch.
//  if (block->outgoing_type == FunctionBlock::kTargetNone) {
//    BasicBlock* next_bb = GetNextBasicBlock();
//    XEASSERTNOTNULL(next_bb);
//    b.CreateBr(next_bb);
//  } else if (block->outgoing_type == FunctionBlock::kTargetUnknown) {
//    // Hrm.
//    // TODO(benvanik): assert this doesn't occur - means a bad sdb run!
//    XELOGCPU("SDB function scan error in %.8X: bb %.8X has unknown exit",
//             fn_->start_address, block->start_address);
//    b.CreateRetVoid();
//  }
//
//  // TODO(benvanik): finish up BB
//}
//
//BasicBlock* LibjitEmitter::GetBasicBlock(uint32_t address) {
//  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(address);
//  if (it != bbs_.end()) {
//    return it->second;
//  }
//  return NULL;
//}
//
//BasicBlock* LibjitEmitter::GetNextBasicBlock() {
//  std::map<uint32_t, BasicBlock*>::iterator it = bbs_.find(
//      fn_block_->start_address);
//  ++it;
//  if (it != bbs_.end()) {
//    return it->second;
//  }
//  return NULL;
//}
//
//BasicBlock* LibjitEmitter::GetReturnBasicBlock() {
//  return return_block_;
//}

//int LibjitEmitter::GenerateIndirectionBranch(uint32_t cia, Value* target,
//                                              bool lk, bool likely_local) {
//  // This function is called by the control emitters when they know that an
//  // indirect branch is required.
//  // It first tries to see if the branch is to an address within the function
//  // and, if so, uses a local switch table. If that fails because we don't know
//  // the block the function is regenerated (ACK!). If the target is external
//  // then an external call occurs.
//
//  IRBuilder<>& b = *builder_;
//  BasicBlock* next_block = GetNextBasicBlock();
//
//  PushInsertPoint();
//
//  // Request builds of the indirection blocks on demand.
//  // We can't build here because we don't know what registers will be needed
//  // yet, so we just create the blocks and let GenerateSharedBlocks handle it
//  // after we are done with all user instructions.
//  if (!external_indirection_block_) {
//    // Setup locals in the entry block.
//    b.SetInsertPoint(&gen_fn_->getEntryBlock());
//    locals_.indirection_target = b.CreateAlloca(
//        b.getInt64Ty(), 0, "indirection_target");
//    locals_.indirection_cia = b.CreateAlloca(
//        b.getInt64Ty(), 0, "indirection_cia");
//
//    external_indirection_block_ = BasicBlock::Create(
//        *context_, "external_indirection_block", gen_fn_, return_block_);
//  }
//  if (likely_local && !internal_indirection_block_) {
//    internal_indirection_block_ = BasicBlock::Create(
//        *context_, "internal_indirection_block", gen_fn_, return_block_);
//  }
//
//  PopInsertPoint();
//
//  // Check to see if the target address is within the function.
//  // If it is jump to that basic block. If the basic block is not found it means
//  // we have a jump inside the function that wasn't identified via static
//  // analysis. These are bad as they require function regeneration.
//  if (likely_local) {
//    // Note that we only support LK=0, as we are using shared tables.
//    XEASSERT(!lk);
//    b.CreateStore(target, locals_.indirection_target);
//    b.CreateStore(b.getInt64(cia), locals_.indirection_cia);
//    Value* fn_ge_cmp = b.CreateICmpUGE(target, b.getInt64(fn_->start_address));
//    Value* fn_l_cmp = b.CreateICmpULT(target, b.getInt64(fn_->end_address));
//    Value* fn_target_cmp = b.CreateAnd(fn_ge_cmp, fn_l_cmp);
//    b.CreateCondBr(fn_target_cmp,
//                   internal_indirection_block_, external_indirection_block_);
//    return 0;
//  }
//
//  // If we are LK=0 jump to the shared indirection block. This prevents us
//  // from needing to fill the registers again after the call and shares more
//  // code.
//  if (!lk) {
//    b.CreateStore(target, locals_.indirection_target);
//    b.CreateStore(b.getInt64(cia), locals_.indirection_cia);
//    b.CreateBr(external_indirection_block_);
//  } else {
//    // Slowest path - spill, call the external function, and fill.
//    // We should avoid this at all costs.
//
//    // Spill registers. We could probably share this.
//    SpillRegisters();
//
//    // TODO(benvanik): keep function pointer lookup local.
//    Value* indirect_branch = gen_module_->getFunction("XeIndirectBranch");
//    b.CreateCall3(indirect_branch,
//                  gen_fn_->arg_begin(),
//                  target,
//                  b.getInt64(cia));
//
//    if (next_block) {
//      // Only refill if not a tail call.
//      FillRegisters();
//      b.CreateBr(next_block);
//    } else {
//      b.CreateRetVoid();
//    }
//  }
//
//  return 0;
//}
//
//Value* LibjitEmitter::LoadStateValue(uint32_t offset, Type* type,
//                                      const char* name) {
//  IRBuilder<>& b = *builder_;
//  PointerType* pointerTy = PointerType::getUnqual(type);
//  Function::arg_iterator args = gen_fn_->arg_begin();
//  Value* state_ptr = args;
//  Value* address = b.CreateInBoundsGEP(state_ptr, b.getInt32(offset));
//  Value* ptr = b.CreatePointerCast(address, pointerTy);
//  return b.CreateLoad(ptr, name);
//}
//
//void LibjitEmitter::StoreStateValue(uint32_t offset, Type* type,
//                                     Value* value) {
//  IRBuilder<>& b = *builder_;
//  PointerType* pointerTy = PointerType::getUnqual(type);
//  Function::arg_iterator args = gen_fn_->arg_begin();
//  Value* state_ptr = args;
//  Value* address = b.CreateInBoundsGEP(state_ptr, b.getInt32(offset));
//  Value* ptr = b.CreatePointerCast(address, pointerTy);
//  b.CreateStore(value, ptr);
//}
//
//void LibjitEmitter::SetupLocals() {
//  IRBuilder<>& b = *builder_;
//
//  uint64_t spr_t = access_bits_.spr;
//  if (spr_t & 0x3) {
//    locals_.xer = SetupLocal(b.getInt64Ty(), "xer");
//  }
//  spr_t >>= 2;
//  if (spr_t & 0x3) {
//    locals_.lr = SetupLocal(b.getInt64Ty(), "lr");
//  }
//  spr_t >>= 2;
//  if (spr_t & 0x3) {
//    locals_.ctr = SetupLocal(b.getInt64Ty(), "ctr");
//  }
//  spr_t >>= 2;
//  // TODO: FPCSR
//
//  char name[32];
//
//  uint64_t cr_t = access_bits_.cr;
//  for (int n = 0; n < 8; n++) {
//    if (cr_t & 3) {
//      xesnprintfa(name, XECOUNT(name), "cr%d", n);
//      locals_.cr[n] = SetupLocal(b.getInt8Ty(), name);
//    }
//    cr_t >>= 2;
//  }
//
//  uint64_t gpr_t = access_bits_.gpr;
//  for (int n = 0; n < 32; n++) {
//    if (gpr_t & 3) {
//      xesnprintfa(name, XECOUNT(name), "r%d", n);
//      locals_.gpr[n] = SetupLocal(b.getInt64Ty(), name);
//    }
//    gpr_t >>= 2;
//  }
//
//  uint64_t fpr_t = access_bits_.fpr;
//  for (int n = 0; n < 32; n++) {
//    if (fpr_t & 3) {
//      xesnprintfa(name, XECOUNT(name), "f%d", n);
//      locals_.fpr[n] = SetupLocal(b.getDoubleTy(), name);
//    }
//    fpr_t >>= 2;
//  }
//}
//
//Value* LibjitEmitter::SetupLocal(llvm::Type* type, const char* name) {
//  IRBuilder<>& b = *builder_;
//  // Insert into the entry block.
//  PushInsertPoint();
//  b.SetInsertPoint(&gen_fn_->getEntryBlock());
//  Value* v = b.CreateAlloca(type, 0, name);
//  PopInsertPoint();
//  return v;
//}
//
//Value* LibjitEmitter::cia_value() {
//  return builder_->getInt32(cia_);
//}
//
//void LibjitEmitter::FillRegisters() {
//  // This updates all of the local register values from the state memory.
//  // It should be called on function entry for initial setup and after any
//  // calls that may modify the registers.
//
//  // TODO(benvanik): use access flags to see if we need to do reads/writes.
//  // Though LLVM may do a better job than we can, except across calls.
//
//  IRBuilder<>& b = *builder_;
//
//  if (locals_.xer) {
//    b.CreateStore(LoadStateValue(
//        offsetof(xe_ppc_state_t, xer),
//        b.getInt64Ty()), locals_.xer);
//  }
//
//  if (locals_.lr) {
//    b.CreateStore(LoadStateValue(
//        offsetof(xe_ppc_state_t, lr),
//        b.getInt64Ty()), locals_.lr);
//  }
//
//  if (locals_.ctr) {
//    b.CreateStore(LoadStateValue(
//        offsetof(xe_ppc_state_t, ctr),
//        b.getInt64Ty()), locals_.ctr);
//  }
//
//  // Fill the split CR values by extracting each one from the CR.
//  // This could probably be done faster via an extractvalues or something.
//  // Perhaps we could also change it to be a vector<8*i8>.
//  Value* cr = NULL;
//  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
//    Value* cr_n = locals_.cr[n];
//    if (!cr_n) {
//      continue;
//    }
//    if (!cr) {
//      cr = LoadStateValue(
//          offsetof(xe_ppc_state_t, cr),
//          b.getInt64Ty());
//    }
//    b.CreateStore(
//        b.CreateTrunc(b.CreateAnd(b.CreateLShr(cr, (28 - n * 4)), 0xF),
//                      b.getInt8Ty()), cr_n);
//  }
//
//  for (size_t n = 0; n < XECOUNT(locals_.gpr); n++) {
//    if (locals_.gpr[n]) {
//      b.CreateStore(LoadStateValue(
//          (uint32_t)offsetof(xe_ppc_state_t, r) + 8 * n,
//          b.getInt64Ty()), locals_.gpr[n]);
//    }
//  }
//
//  for (size_t n = 0; n < XECOUNT(locals_.fpr); n++) {
//    if (locals_.fpr[n]) {
//      b.CreateStore(LoadStateValue(
//          (uint32_t)offsetof(xe_ppc_state_t, f) + 8 * n,
//          b.getDoubleTy()), locals_.fpr[n]);
//    }
//  }
//}
//
//void LibjitEmitter::SpillRegisters() {
//  // This flushes all local registers (if written) to the register bank and
//  // resets their values.
//  //
//  // TODO(benvanik): only flush if actually required, or selective flushes.
//
//  IRBuilder<>& b = *builder_;
//
//  if (locals_.xer) {
//    StoreStateValue(
//        offsetof(xe_ppc_state_t, xer),
//        b.getInt64Ty(),
//        b.CreateLoad(locals_.xer));
//  }
//
//  if (locals_.lr) {
//    StoreStateValue(
//        offsetof(xe_ppc_state_t, lr),
//        b.getInt64Ty(),
//        b.CreateLoad(locals_.lr));
//  }
//
//  if (locals_.ctr) {
//    StoreStateValue(
//        offsetof(xe_ppc_state_t, ctr),
//        b.getInt64Ty(),
//        b.CreateLoad(locals_.ctr));
//  }
//
//  // Stitch together all split CR values.
//  // TODO(benvanik): don't flush across calls?
//  Value* cr = NULL;
//  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
//    Value* cr_n = locals_.cr[n];
//    if (!cr_n) {
//      continue;
//    }
//    cr_n = b.CreateZExt(b.CreateLoad(cr_n), b.getInt64Ty());
//    if (!cr) {
//      cr = b.CreateShl(cr_n, n * 4);
//    } else {
//      cr = b.CreateOr(cr, b.CreateShl(cr_n, n * 4));
//    }
//  }
//  if (cr) {
//    StoreStateValue(
//        offsetof(xe_ppc_state_t, cr),
//        b.getInt64Ty(),
//        cr);
//  }
//
//  for (uint32_t n = 0; n < XECOUNT(locals_.gpr); n++) {
//    Value* v = locals_.gpr[n];
//    if (v) {
//      StoreStateValue(
//          offsetof(xe_ppc_state_t, r) + 8 * n,
//          b.getInt64Ty(),
//          b.CreateLoad(locals_.gpr[n]));
//    }
//  }
//
//  for (uint32_t n = 0; n < XECOUNT(locals_.fpr); n++) {
//    Value* v = locals_.fpr[n];
//    if (v) {
//      StoreStateValue(
//          offsetof(xe_ppc_state_t, f) + 8 * n,
//          b.getDoubleTy(),
//          b.CreateLoad(locals_.fpr[n]));
//    }
//  }
//}
//
//Value* LibjitEmitter::xer_value() {
//  XEASSERTNOTNULL(locals_.xer);
//  IRBuilder<>& b = *builder_;
//  return b.CreateLoad(locals_.xer);
//}
//
//void LibjitEmitter::update_xer_value(Value* value) {
//  XEASSERTNOTNULL(locals_.xer);
//  IRBuilder<>& b = *builder_;
//
//  // Extend to 64bits if needed.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//  b.CreateStore(value, locals_.xer);
//}
//
//void LibjitEmitter::update_xer_with_overflow(Value* value) {
//  XEASSERTNOTNULL(locals_.xer);
//  IRBuilder<>& b = *builder_;
//
//  // Expects a i1 indicating overflow.
//  // Trust the caller that if it's larger than that it's already truncated.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//
//  Value* xer = xer_value();
//  xer = b.CreateAnd(xer, 0xFFFFFFFFBFFFFFFF); // clear bit 30
//  xer = b.CreateOr(xer, b.CreateShl(value, 31));
//  xer = b.CreateOr(xer, b.CreateShl(value, 30));
//  b.CreateStore(xer, locals_.xer);
//}
//
//void LibjitEmitter::update_xer_with_carry(Value* value) {
//  XEASSERTNOTNULL(locals_.xer);
//  IRBuilder<>& b = *builder_;
//
//  // Expects a i1 indicating carry.
//  // Trust the caller that if it's larger than that it's already truncated.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//
//  Value* xer = xer_value();
//  xer = b.CreateAnd(xer, 0xFFFFFFFFDFFFFFFF); // clear bit 29
//  xer = b.CreateOr(xer, b.CreateShl(value, 29));
//  b.CreateStore(xer, locals_.xer);
//}
//
//void LibjitEmitter::update_xer_with_overflow_and_carry(Value* value) {
//  XEASSERTNOTNULL(locals_.xer);
//  IRBuilder<>& b = *builder_;
//
//  // Expects a i1 indicating overflow.
//  // Trust the caller that if it's larger than that it's already truncated.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//
//  // This is effectively an update_xer_with_overflow followed by an
//  // update_xer_with_carry, but since the logic is largely the same share it.
//  Value* xer = xer_value();
//  xer = b.CreateAnd(xer, 0xFFFFFFFF9FFFFFFF); // clear bit 30 & 29
//  xer = b.CreateOr(xer, b.CreateShl(value, 31));
//  xer = b.CreateOr(xer, b.CreateShl(value, 30));
//  xer = b.CreateOr(xer, b.CreateShl(value, 29));
//  b.CreateStore(xer, locals_.xer);
//}
//
//Value* LibjitEmitter::lr_value() {
//  XEASSERTNOTNULL(locals_.lr);
//  IRBuilder<>& b = *builder_;
//  return b.CreateLoad(locals_.lr);
//}
//
//void LibjitEmitter::update_lr_value(Value* value) {
//  XEASSERTNOTNULL(locals_.lr);
//  IRBuilder<>& b = *builder_;
//
//  // Extend to 64bits if needed.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//  b.CreateStore(value, locals_.lr);
//}
//
//Value* LibjitEmitter::ctr_value() {
//  XEASSERTNOTNULL(locals_.ctr);
//  IRBuilder<>& b = *builder_;
//
//  return b.CreateLoad(locals_.ctr);
//}
//
//void LibjitEmitter::update_ctr_value(Value* value) {
//  XEASSERTNOTNULL(locals_.ctr);
//  IRBuilder<>& b = *builder_;
//
//  // Extend to 64bits if needed.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//  b.CreateStore(value, locals_.ctr);
//}
//
//Value* LibjitEmitter::cr_value(uint32_t n) {
//  XEASSERT(n >= 0 && n < 8);
//  XEASSERTNOTNULL(locals_.cr[n]);
//  IRBuilder<>& b = *builder_;
//
//  Value* v = b.CreateLoad(locals_.cr[n]);
//  v = b.CreateZExt(v, b.getInt64Ty());
//  return v;
//}
//
//void LibjitEmitter::update_cr_value(uint32_t n, Value* value) {
//  XEASSERT(n >= 0 && n < 8);
//  XEASSERTNOTNULL(locals_.cr[n]);
//  IRBuilder<>& b = *builder_;
//
//  // Truncate to 8 bits if needed.
//  // TODO(benvanik): also widen?
//  if (!value->getType()->isIntegerTy(8)) {
//    value = b.CreateTrunc(value, b.getInt8Ty());
//  }
//
//  b.CreateStore(value, locals_.cr[n]);
//}
//
//void LibjitEmitter::update_cr_with_cond(
//    uint32_t n, Value* lhs, Value* rhs, bool is_signed) {
//  IRBuilder<>& b = *builder_;
//
//  // bit0 = RA < RB
//  // bit1 = RA > RB
//  // bit2 = RA = RB
//  // bit3 = XER[SO]
//
//  // TODO(benvanik): inline this using the x86 cmp instruction - this prevents
//  // the need for a lot of the compares and ensures we lower to the best
//  // possible x86.
//  // Value* cmp = InlineAsm::get(
//  //     FunctionType::get(),
//  //     "cmp $0, $1                 \n"
//  //     "mov from compare registers \n",
//  //     "r,r", ??
//  //     true);
//
//  Value* is_lt = is_signed ?
//      b.CreateICmpSLT(lhs, rhs) : b.CreateICmpULT(lhs, rhs);
//  Value* is_gt = is_signed ?
//      b.CreateICmpSGT(lhs, rhs) : b.CreateICmpUGT(lhs, rhs);
//  Value* cp = b.CreateSelect(is_gt, b.getInt8(1 << 1), b.getInt8(1 << 2));
//  Value* c = b.CreateSelect(is_lt, b.getInt8(1 << 0), cp);
//
//  // TODO(benvanik): set bit 4 to XER[SO]
//
//  // Insert the 4 bits into their location in the CR.
//  update_cr_value(n, c);
//}
//
//Value* LibjitEmitter::gpr_value(uint32_t n) {
//  XEASSERT(n >= 0 && n < 32);
//  XEASSERTNOTNULL(locals_.gpr[n]);
//  IRBuilder<>& b = *builder_;
//
//  // Actually r0 is writable, even though nobody should ever do that.
//  // Perhaps we can check usage and enable this if safe?
//  // if (n == 0) {
//  //   // Always force zero to a constant - this should help LLVM.
//  //   return b.getInt64(0);
//  // }
//
//  return b.CreateLoad(locals_.gpr[n]);
//}
//
//void LibjitEmitter::update_gpr_value(uint32_t n, Value* value) {
//  XEASSERT(n >= 0 && n < 32);
//  XEASSERTNOTNULL(locals_.gpr[n]);
//  IRBuilder<>& b = *builder_;
//
//  // See above - r0 can be written.
//  // if (n == 0) {
//  //   // Ignore writes to zero.
//  //   return;
//  // }
//
//  // Extend to 64bits if needed.
//  if (!value->getType()->isIntegerTy(64)) {
//    value = b.CreateZExt(value, b.getInt64Ty());
//  }
//
//  b.CreateStore(value, locals_.gpr[n]);
//}
//
//Value* LibjitEmitter::fpr_value(uint32_t n) {
//  XEASSERT(n >= 0 && n < 32);
//  XEASSERTNOTNULL(locals_.fpr[n]);
//  IRBuilder<>& b = *builder_;
//  return b.CreateLoad(locals_.fpr[n]);
//}
//
//void LibjitEmitter::update_fpr_value(uint32_t n, Value* value) {
//  XEASSERT(n >= 0 && n < 32);
//  XEASSERTNOTNULL(locals_.fpr[n]);
//  IRBuilder<>& b = *builder_;
//  value = b.CreateFPExtOrFPTrunc(value, b.getDoubleTy());
//  b.CreateStore(value, locals_.fpr[n]);
//}
//
//Value* LibjitEmitter::GetMembase() {
//  Value* v = gen_module_->getGlobalVariable("xe_memory_base");
//  return builder_->CreateLoad(v);
//}
//
//Value* LibjitEmitter::GetMemoryAddress(uint32_t cia, Value* addr) {
//  IRBuilder<>& b = *builder_;
//
//  // Input address is always in 32-bit space.
//  addr = b.CreateAnd(addr, UINT_MAX);
//
//  // Add runtime memory address checks, if needed.
//  if (FLAGS_memory_address_verification) {
//    BasicBlock* invalid_bb = BasicBlock::Create(*context_, "", gen_fn_);
//    BasicBlock* valid_bb = BasicBlock::Create(*context_, "", gen_fn_);
//
//    // The heap starts at 0x1000 - if we write below that we're boned.
//    Value* gt = b.CreateICmpUGE(addr, b.getInt64(0x00001000));
//    b.CreateCondBr(gt, valid_bb, invalid_bb);
//
//    b.SetInsertPoint(invalid_bb);
//    Value* access_violation = gen_module_->getFunction("XeAccessViolation");
//    SpillRegisters();
//    b.CreateCall3(access_violation,
//                  gen_fn_->arg_begin(),
//                  b.getInt32(cia),
//                  addr);
//    b.CreateBr(valid_bb);
//
//    b.SetInsertPoint(valid_bb);
//  }
//
//  // Rebase off of memory base pointer.
//  return b.CreateInBoundsGEP(GetMembase(), addr);
//}
//
//Value* LibjitEmitter::ReadMemory(
//    uint32_t cia, Value* addr, uint32_t size, bool acquire) {
//  IRBuilder<>& b = *builder_;
//
//  Type* dataTy = NULL;
//  bool needs_swap = false;
//  switch (size) {
//    case 1:
//      dataTy = b.getInt8Ty();
//      break;
//    case 2:
//      dataTy = b.getInt16Ty();
//      needs_swap = true;
//      break;
//    case 4:
//      dataTy = b.getInt32Ty();
//      needs_swap = true;
//      break;
//    case 8:
//      dataTy = b.getInt64Ty();
//      needs_swap = true;
//      break;
//    default:
//      XEASSERTALWAYS();
//      return NULL;
//  }
//  PointerType* pointerTy = PointerType::getUnqual(dataTy);
//
//  Value* address = GetMemoryAddress(cia, addr);
//  Value* ptr = b.CreatePointerCast(address, pointerTy);
//  LoadInst* load_value = b.CreateLoad(ptr);
//  if (acquire) {
//    load_value->setAlignment(size);
//    load_value->setVolatile(true);
//    load_value->setAtomic(Acquire);
//  }
//  Value* value = load_value;
//
//  // Swap after loading.
//  // TODO(benvanik): find a way to avoid this!
//  if (needs_swap) {
//    Function* bswap = Intrinsic::getDeclaration(
//          gen_module_, Intrinsic::bswap, dataTy);
//    value = b.CreateCall(bswap, value);
//  }
//
//  return value;
//}
//
//void LibjitEmitter::WriteMemory(
//    uint32_t cia, Value* addr, uint32_t size, Value* value, bool release) {
//  IRBuilder<>& b = *builder_;
//
//  Type* dataTy = NULL;
//  bool needs_swap = false;
//  switch (size) {
//    case 1:
//      dataTy = b.getInt8Ty();
//      break;
//    case 2:
//      dataTy = b.getInt16Ty();
//      needs_swap = true;
//      break;
//    case 4:
//      dataTy = b.getInt32Ty();
//      needs_swap = true;
//      break;
//    case 8:
//      dataTy = b.getInt64Ty();
//      needs_swap = true;
//      break;
//    default:
//      XEASSERTALWAYS();
//      return;
//  }
//  PointerType* pointerTy = PointerType::getUnqual(dataTy);
//
//  Value* address = GetMemoryAddress(cia, addr);
//  Value* ptr = b.CreatePointerCast(address, pointerTy);
//
//  // Truncate, if required.
//  if (value->getType() != dataTy) {
//    value = b.CreateTrunc(value, dataTy);
//  }
//
//  // Swap before storing.
//  // TODO(benvanik): find a way to avoid this!
//  if (needs_swap) {
//    Function* bswap = Intrinsic::getDeclaration(
//          gen_module_, Intrinsic::bswap, dataTy);
//    value = b.CreateCall(bswap, value);
//  }
//
//  StoreInst* store_value = b.CreateStore(value, ptr);
//  if (release) {
//    store_value->setAlignment(size);
//    store_value->setVolatile(true);
//    store_value->setAtomic(Release);
//  }
//}
