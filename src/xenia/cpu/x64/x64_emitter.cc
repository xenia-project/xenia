/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_emitter.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/ppc/state.h>

#include <beaengine/BeaEngine.h>


using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::cpu::x64;

using namespace AsmJit;


DEFINE_bool(memory_address_verification, false,
    "Whether to add additional checks to generated memory load/stores.");
DEFINE_bool(cache_registers, false,
    "Cache PPC registers inside of functions.");

DEFINE_bool(log_codegen, false,
    "Log codegen to stdout.");
DEFINE_bool(annotate_disassembly, true,
    "Annotate disassembled x64 code with comments.");


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


X64Emitter::X64Emitter(xe_memory_ref memory) :
    memory_(memory), logger_(NULL) {
  // I don't like doing this, but there's no public access to these members.
  assembler_._properties = compiler_._properties;

  // Grab global exports.
  cpu::GetGlobalExports(&global_exports_);

  // Lock used for all codegen.
  // It'd be nice to have multiple emitters/etc that could be used to prevent
  // contention of the JIT.
  lock_ = xe_mutex_alloc(10000);
  XEASSERTNOTNULL(lock_);

  // Setup logging.
  if (FLAGS_log_codegen) {
    logger_ = new FileLogger(stdout);
    logger_->setEnabled(true);
    assembler_.setLogger(logger_);
    compiler_.setLogger(logger_);
  }
}

X64Emitter::~X64Emitter() {
  Lock();

  assembler_.clear();
  compiler_.clear();
  delete logger_;

  Unlock();

  xe_mutex_free(lock_);
  lock_ = NULL;
}

void X64Emitter::Lock() {
  xe_mutex_lock(lock_);
}

void X64Emitter::Unlock() {
  xe_mutex_unlock(lock_);
}

int X64Emitter::PrepareFunction(FunctionSymbol* symbol) {
  int result_code = 1;
  Lock();

  if (symbol->impl_value) {
    result_code = 0;
    XESUCCEED();
  }

  XEASSERT(symbol->type != FunctionSymbol::Unknown);

  // Create the custom redirector function.
  // This function will jump to the on-demand compilation routine to
  // generate the real function as required. Afterwards, it will be
  // overwritten with a jump to the new function.

  if (logger_) {
    logger_->setEnabled(false);
  }

  // PrepareFunction:
  // ; mov rcx, ppc_state -- comes in as arg
  // ; mov rdx, lr        -- comes in as arg
  // mov r8, [emitter]
  // mov r9, [symbol]
  // call [OnDemandCompileTrampoline]
  // jmp [rax]

#if defined(ASMJIT_WINDOWS)
  // Calling convetion: kX86FuncConvX64W
  // Arguments passed as RCX, RDX, R8, R9
  assembler_.push(rcx); // ppc_state
  assembler_.push(rdx); // lr
  assembler_.sub(rsp, imm(0x20));
  assembler_.mov(rcx, imm((uint64_t)this));
  assembler_.mov(rdx, imm((uint64_t)symbol));
  assembler_.call(X64Emitter::OnDemandCompileTrampoline);
  assembler_.add(rsp, imm(0x20));
  assembler_.pop(rdx); // lr
  assembler_.pop(rcx); // ppc_state
  assembler_.jmp(rax);
#else
  // Calling convetion: kX86FuncConvX64U
  // Arguments passed as RDI, RSI, RDX, RCX, R8, R9
  assembler_.push(rdi); // ppc_state
  assembler_.push(rsi); // lr
  assembler_.sub(rsp, imm(0x20));
  assembler_.mov(rdi, imm((uint64_t)this));
  assembler_.mov(rsi, imm((uint64_t)symbol));
  assembler_.call(X64Emitter::OnDemandCompileTrampoline);
  assembler_.add(rsp, imm(0x20));
  assembler_.pop(rsi); // lr
  assembler_.pop(rdi); // ppc_state
  assembler_.jmp(rax);
#endif  // ASM_JIT_WINDOWS

  // Assemble and stash.
  void* fn_ptr = assembler_.make();
  symbol->impl_value = fn_ptr;
  symbol->impl_size = assembler_.getCodeSize();

  result_code = 0;
XECLEANUP:
  assembler_.clear();
  if (logger_) {
    logger_->setEnabled(true);
  }
  Unlock();
  return result_code;
}

void* X64Emitter::OnDemandCompileTrampoline(
    X64Emitter* emitter, FunctionSymbol* symbol) {
  // This function is called by the redirector code from PrepareFunction.
  // We jump into the member OnDemandCompile and pass back the
  // result.
  return emitter->OnDemandCompile(symbol);
}

void* X64Emitter::OnDemandCompile(FunctionSymbol* symbol) {
  void* redirector_ptr = symbol->impl_value;
  size_t redirector_size = symbol->impl_size;

  // Generate the real function.
  int result_code = MakeFunction(symbol);
  if (result_code) {
    // Failed to make the function! We're hosed!
    // We'll likely crash now.
    XELOGCPU("Compile(%s): failed to make function", symbol->name());
    XEASSERTALWAYS();
    return 0;
  }

  // TODO(benvanik): find a way to patch in that is thread safe?
  // Overwrite the redirector function to jump to the new one.
  // This preserves the arguments passed to the redirector.
  uint8_t* bp = (uint8_t*)redirector_ptr;
  size_t o = 0;
  uint64_t target_ptr = (uint64_t)symbol->impl_value;
  if (target_ptr & ~0xFFFFFFFFLL) {
    // mov rax, imm64
    bp[o++] = 0x48; bp[o++] = 0xB8;
    for (int n = 0; n < 8; n++) {
      bp[o++] = (target_ptr >> (n * 8)) & 0xFF;
    }
  } else {
    // mov rax, imm32
    bp[o++] = 0x48; bp[o++] = 0xC7; bp[o++] = 0xC0;
    for (int n = 0; n < 4; n++) {
      bp[o++] = (target_ptr >> (n * 8)) & 0xFF;
    }
  }
  // jmp rax
  bp[o++] = 0xFF; bp[o++] = 0xE0;

  // Write some no-ops to cover up the rest of the redirection.
  // NOTE: not currently doing this as we overwrite the code that
  //       got us here and endup just running nops.
  // while (o < redirector_size) {
  //   bp[o++] = 0x90;
  // }

  return symbol->impl_value;
}

int X64Emitter::MakeFunction(FunctionSymbol* symbol) {
  X86Compiler& c = compiler_;

  int result_code = 1;
  Lock();

  XELOGCPU("Compile(%s): beginning compilation...", symbol->name());

  symbol_ = symbol;
  fn_block_ = NULL;

  return_block_ = Label();
  internal_indirection_block_ = Label();
  external_indirection_block_ = Label();

  bbs_.clear();

  access_bits_.Clear();

  locals_.indirection_target = GpVar();
  locals_.indirection_cia = GpVar();

  locals_.xer = GpVar();
  locals_.lr = GpVar();
  locals_.ctr = GpVar();
  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
    locals_.cr[n] = GpVar();
  }
  for (size_t n = 0; n < XECOUNT(locals_.gpr); n++) {
    locals_.gpr[n] = GpVar();
  }
  for (size_t n = 0; n < XECOUNT(locals_.fpr); n++) {
    locals_.fpr[n] = GpVar();
  }

  // Setup function. All share the same signature.
  compiler_.newFunc(kX86FuncConvDefault,
      FuncBuilder2<void, void*, uint64_t>());

  // Elevate the priority of ppc_state, as it's often used.
  // TODO(benvanik): evaluate if this is a good idea.
  compiler_.setPriority(compiler_.getGpArg(0), 100);

  switch (symbol->type) {
  case FunctionSymbol::Kernel:
    if (symbol->kernel_export && symbol->kernel_export->is_implemented) {
      result_code = MakePresentImportFunction();
    } else {
      result_code = MakeMissingImportFunction();
    }
    break;
  case FunctionSymbol::User:
    result_code = MakeUserFunction();
    break;
  default:
    XEASSERTALWAYS();
    result_code = 1;
    break;
  }
  XEEXPECTZERO(result_code);

  compiler_.endFunc();

  // Serialize to the assembler.
  compiler_.serialize(assembler_);
  if (compiler_.getError()) {
    result_code = 2;
  } else if (assembler_.getError()) {
    result_code = 3;
  }
  XEEXPECTZERO(result_code);

  // Perform final assembly/relocation.
  symbol->impl_value = assembler_.make();

  if (FLAGS_log_codegen) {
    XELOGCPU("Compile(%s): compiled to 0x%p (%db)",
        symbol->name(),
        assembler_.getCode(), assembler_.getCodeSize());

    // Dump x64 assembly.
    // This is not currently used as we are dumping from asmjit.
    // This format is more concise and prettier (though it lacks comments).
#if 0
    DISASM MyDisasm;
    memset(&MyDisasm, 0, sizeof(MyDisasm));
    MyDisasm.Archi = 64;
    MyDisasm.Options = Tabulation + MasmSyntax + PrefixedNumeral;
    MyDisasm.EIP = (UIntPtr)assembler_.getCode();
    void* eip_end = assembler_.getCode() + assembler_.getCodeSize();
    while (MyDisasm.EIP < (UIntPtr)eip_end) {
      size_t len = Disasm(&MyDisasm);
      if (len == UNKNOWN_OPCODE) {
        break;
      }
      XELOGCPU("%p  %s", MyDisasm.EIP, MyDisasm.CompleteInstr);
      MyDisasm.EIP += len;
    }
#endif
  }

  result_code = 0;
XECLEANUP:
  // Cleanup assembler/compiler. We keep them around to reuse their buffers.
  assembler_.clear();
  compiler_.clear();

  Unlock();
  return result_code;
}

int X64Emitter::MakePresentImportFunction() {
  X86Compiler& c = compiler_;

  TraceKernelCall();

  void* shim = symbol_->kernel_export->function_data.shim;
  void* shim_data = symbol_->kernel_export->function_data.shim_data;

  // void shim(ppc_state*, shim_data*)
  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)shim_data));
  X86CompilerFuncCall* call = c.call(shim);
  call->setComment(symbol_->kernel_export->name);
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder2<void, void*, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);

  c.ret();

  return 0;
}

int X64Emitter::MakeMissingImportFunction() {
  X86Compiler& c = compiler_;

  TraceKernelCall();

  // TODO(benvanik): log better?
  c.ret();

  return 0;
}

int X64Emitter::MakeUserFunction() {
  X86Compiler& c = compiler_;

  TraceUserCall();

  // If this function is empty, abort!
  if (!symbol_->blocks.size()) {
    c.ret();
    return 0;
  }

  // Pass 1 creates all of the labels - this way we can branch to them.
  // We also track registers used so that when know which ones to fill/spill.
  // No actual blocks or instructions are created here.
  // TODO(benvanik): move this to SDB? would remove an entire pass over the
  //     code.
  for (std::map<uint32_t, FunctionBlock*>::iterator it =
      symbol_->blocks.begin(); it != symbol_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    XEIGNORE(PrepareBasicBlock(block));
  }

  // Setup all local variables now that we know what we need.
  // This happens in the entry block.
  SetupLocals();

  // Setup initial register fill in the entry block.
  // We can only do this once all the locals have been created.
  FillRegisters();

  // Pass 2 fills in instructions.
  for (std::map<uint32_t, FunctionBlock*>::iterator it =
      symbol_->blocks.begin(); it != symbol_->blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    GenerateBasicBlock(block);
  }

  // Setup the shared return/indirection/etc blocks now that we know all the
  // blocks we need and all the registers used.
  GenerateSharedBlocks();

  return 0;
}

X86Compiler& X64Emitter::compiler() {
  return compiler_;
}

FunctionSymbol* X64Emitter::symbol() {
  return symbol_;
}

FunctionBlock* X64Emitter::fn_block() {
  return fn_block_;
}

void X64Emitter::GenerateSharedBlocks() {
  X86Compiler& c = compiler_;

  // Create a return block, if it was used.
  if (return_block_.getId() != kInvalidValue) {
    // This spills registers and returns. All non-tail returns should branch
    // here to do the return and ensure registers are spilled.
    // This will be moved to the end after all the other blocks are created.
    if (FLAGS_annotate_disassembly) {
      c.comment("Shared return block");
    }
    c.bind(return_block_);
    SpillRegisters();
    c.ret();
  }

  // Build indirection block on demand.
  // We have already prepped all basic blocks, so we can build these tables now.
  if (external_indirection_block_.getId() != kInvalidValue) {
    // This will spill registers and call the external function.
    // It is only meant for LK=0.
    c.bind(external_indirection_block_);
    if (FLAGS_annotate_disassembly) {
      c.comment("Shared external indirection block");
    }
    SpillRegisters();
    X86CompilerFuncCall* call = c.call(global_exports_.XeIndirectBranch);
    call->setPrototype(kX86FuncConvDefault,
        FuncBuilder3<void*, void*, uint64_t, uint64_t>());
    call->setArgument(0, c.getGpArg(0));
    call->setArgument(1, locals_.indirection_target);
    call->setArgument(2, locals_.indirection_cia);
    GpVar target_ptr(c.newGpVar());
    call->setReturn(target_ptr);

    // Call target.
    // void fn(ppc_state*, uint64_t)
    call = c.call(target_ptr);
    call->setComment("Indirection branch");
    call->setPrototype(kX86FuncConvDefault,
        FuncBuilder2<void, void*, uint64_t>());
    call->setArgument(0, c.getGpArg(0));
    call->setArgument(1, locals_.indirection_cia);
    c.ret();
  }

  if (internal_indirection_block_.getId() != kInvalidValue) {
    // This will not spill registers and instead try to switch on local blocks.
    // If it fails then the external indirection path is taken.
    // NOTE: we only generate this if a likely local branch is taken.
    c.bind(internal_indirection_block_);
    if (FLAGS_annotate_disassembly) {
      c.comment("Shared internal indirection block");
    }
    c.int3();
    // SwitchInst* switch_i = b.CreateSwitch(
    //     b.CreateLoad(locals_.indirection_target),
    //     external_indirection_block_,
    //     static_cast<int>(bbs_.size()));
    // for (std::map<uint32_t, BasicBlock*>::iterator it = bbs_.begin();
    //      it != bbs_.end(); ++it) {
    //   switch_i->addCase(b.getInt64(it->first), it->second);
    // }
  }
}

int X64Emitter::PrepareBasicBlock(FunctionBlock* block) {
  X86Compiler& c = compiler_;

  // Add an undefined entry in the table.
  // The label will be created on-demand.
  bbs_.insert(std::pair<uint32_t, Label>(block->start_address, c.newLabel()));

  // Scan and disassemble each instruction in the block to get accurate
  // register access bits. In the future we could do other optimization checks
  // in this pass.
  // TODO(benvanik): perhaps we want to stash this for each basic block?
  // We could use this for faster checking of cr/ca checks/etc.
  InstrAccessBits access_bits;
  uint8_t* p = xe_memory_addr(memory_, 0);
  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
    InstrData i;
    i.address = ia;
    i.code = XEGETUINT32BE(p + ia);
    i.type = ppc::GetInstrType(i.code);

    // Ignore unknown or ones with no disassembler fn.
    if (!i.type || !i.type->disassemble) {
      continue;
    }

    // We really need to know the registers modified, so die if we've been lazy
    // and haven't implemented the disassemble method yet.
    ppc::InstrDisasm d;
    XEASSERTNOTNULL(i.type->disassemble);
    int result_code = i.type->disassemble(i, d);
    XEASSERTZERO(result_code);
    if (result_code) {
      return result_code;
    }

    // Accumulate access bits.
    access_bits.Extend(d.access_bits);
  }

  // Add in access bits to function access bits.
  access_bits_.Extend(access_bits);

  return 0;
}

void X64Emitter::GenerateBasicBlock(FunctionBlock* block) {
  X86Compiler& c = compiler_;

  // Create new block.
  fn_block_ = block;

  if (FLAGS_log_codegen) {
    printf("  bb %.8X-%.8X:\n", block->start_address, block->end_address);
  }
  if (FLAGS_annotate_disassembly) {
    c.comment("bb %.8X - %.8X", block->start_address, block->end_address);
  }

  // This will create a label if it hasn't already been done.
  std::map<uint32_t, Label>::iterator label_it =
      bbs_.find(block->start_address);
  XEASSERT(label_it != bbs_.end());
  c.bind(label_it->second);

  // Walk instructions in block.
  uint8_t* p = xe_memory_addr(memory_, 0);
  for (uint32_t ia = block->start_address; ia <= block->end_address; ia += 4) {
    InstrData i;
    i.address = ia;
    i.code = XEGETUINT32BE(p + ia);
    i.type = ppc::GetInstrType(i.code);

    // Add debugging tag.
    // TODO(benvanik): add debugging info?

    if (FLAGS_log_codegen || FLAGS_annotate_disassembly) {
      if (!i.type) {
        if (FLAGS_log_codegen) {
          printf("%.8X: %.8X ???", ia, i.code);
        }
        if (FLAGS_annotate_disassembly) {
          c.comment("%.8X: %.8X ???", ia, i.code);
        }
      } else if (i.type->disassemble) {
        ppc::InstrDisasm d;
        i.type->disassemble(i, d);
        std::string disasm;
        d.Dump(disasm);
        if (FLAGS_log_codegen) {
          printf("    %.8X: %.8X %s\n", ia, i.code, disasm.c_str());
        }
        if (FLAGS_annotate_disassembly) {
          c.comment("%.8X: %.8X %s", ia, i.code, disasm.c_str());
        }
      } else {
        if (FLAGS_log_codegen) {
          printf("    %.8X: %.8X %s ???\n", ia, i.code, i.type->name);
        }
        if (FLAGS_annotate_disassembly) {
          c.comment("%.8X: %.8X %s ???", ia, i.code, i.type->name);
        }
      }
    }

    if (FLAGS_log_codegen) {
      fflush(stdout);
    }

    TraceInstruction(i);

    if (!i.type) {
      XELOGCPU("Invalid instruction %.8X %.8X", ia, i.code);
      TraceInvalidInstruction(i);
      continue;
    }

    typedef int (*InstrEmitter)(X64Emitter& g, X86Compiler& c, InstrData& i);
    InstrEmitter emit = (InstrEmitter)i.type->emit;
    if (!i.type->emit || emit(*this, compiler_, i)) {
      // This printf is handy for sort/uniquify to find instructions.
      printf("unimplinstr %s\n", i.type->name);

      XELOGCPU("Unimplemented instr %.8X %.8X %s",
               ia, i.code, i.type->name);
      TraceInvalidInstruction(i);
    }
  }

  // If we fall through, create the branch.
  if (block->outgoing_type == FunctionBlock::kTargetNone) {
    // BasicBlock* next_bb = GetNextBasicBlock();
    // XEASSERTNOTNULL(next_bb);
    // b.CreateBr(next_bb);
  } else if (block->outgoing_type == FunctionBlock::kTargetUnknown) {
    // Hrm.
    // TODO(benvanik): assert this doesn't occur - means a bad sdb run!
    XELOGCPU("SDB function scan error in %.8X: bb %.8X has unknown exit",
             symbol_->start_address, block->start_address);
    c.ret();
  }

  // TODO(benvanik): finish up BB
}

Label& X64Emitter::GetReturnLabel() {
  X86Compiler& c = compiler_;
  // Implicit creation on first use.
  if (return_block_.getId() == kInvalidValue) {
     return_block_ = c.newLabel();
  }
  return return_block_;
}

Label& X64Emitter::GetBlockLabel(uint32_t address) {
  std::map<uint32_t, Label>::iterator it = bbs_.find(address);
  return it->second;
}

int X64Emitter::CallFunction(FunctionSymbol* target_symbol,
                             GpVar& lr, bool tail) {
  X86Compiler& c = compiler_;

  // Prep the target function.
  // If the target function was small we could try to make the whole thing now.
  PrepareFunction(target_symbol);

  SpillRegisters();

  uint64_t target_ptr = (uint64_t)target_symbol->impl_value;
  XEASSERTNOTNULL(target_ptr);

#if 0
  if (tail) {
    // Tail calls are just jumps.
#if defined(ASMJIT_WINDOWS)
    // Calling convetion: kX86FuncConvX64W
    // Arguments passed as RCX, RDX, R8, R9
    c.alloc(c.getGpArg(0), rcx);
    c.alloc(lr, rdx);
    // TODO(benvanik): just use jmp when fixed: https://code.google.com/p/asmjit/issues/detail?id=67
    // c.jmp(target_ptr);
#else
    // Calling convetion: kX86FuncConvX64U
    // Arguments passed as RDI, RSI, RDX, RCX, R8, R9
    c.alloc(c.getGpArg(0), rdi);
    c.alloc(lr, rsi);
    // TODO(benvanik): just use jmp when fixed: https://code.google.com/p/asmjit/issues/detail?id=67
    // c.jmp(target_ptr);
#endif  // ASMJIT_WINDOWS
  } else {
#else
  {
#endif  // tail call disable
    // void fn(ppc_state*, uint64_t)
    X86CompilerFuncCall* call = c.call(target_ptr);
    call->setComment(target_symbol->name());
    call->setPrototype(kX86FuncConvDefault,
        FuncBuilder2<void, void*, uint64_t>());
    call->setArgument(0, c.getGpArg(0));
    call->setArgument(1, lr);
    if (tail) {
      c.ret();
    }
  }

  return 0;
}

void X64Emitter::TraceKernelCall() {
  X86Compiler& c = compiler_;

  if (!FLAGS_trace_kernel_calls) {
    return;
  }

  for (int n = 0; n < 5; n++) {
    c.nop();
  }

  if (FLAGS_annotate_disassembly) {
    c.comment("XeTraceKernelCall (+spill)");
  }

  SpillRegisters();

  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)symbol_->start_address));
  GpVar arg3 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg3, imm((uint64_t)symbol_->kernel_export));
  X86CompilerFuncCall* call = c.call(global_exports_.XeTraceKernelCall);
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder4<void, void*, uint64_t, uint64_t, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);
  call->setArgument(2, c.getGpArg(1));
  call->setArgument(3, arg3);

  for (int n = 0; n < 2; n++) {
    c.nop();
  }
}

void X64Emitter::TraceUserCall() {
  X86Compiler& c = compiler_;

  if (!FLAGS_trace_user_calls) {
    return;
  }

  for (int n = 0; n < 5; n++) {
    c.nop();
  }

  if (FLAGS_annotate_disassembly) {
    c.comment("XeTraceUserCall (+spill)");
  }

  SpillRegisters();

  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)symbol_->start_address));
  GpVar arg3 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg3, imm((uint64_t)symbol_));
  X86CompilerFuncCall* call = c.call(global_exports_.XeTraceUserCall);
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder4<void, void*, uint64_t, uint64_t, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);
  call->setArgument(2, c.getGpArg(1));
  call->setArgument(3, arg3);

  for (int n = 0; n < 2; n++) {
    c.nop();
  }
}

void X64Emitter::TraceInstruction(InstrData& i) {
  X86Compiler& c = compiler_;

  if (!FLAGS_trace_instructions) {
    return;
  }

  for (int n = 0; n < 5; n++) {
    c.nop();
  }

  if (FLAGS_annotate_disassembly) {
    c.comment("XeTraceInstruction (+spill)");
  }

  SpillRegisters();

  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)i.address));
  GpVar arg2 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg2, imm((uint64_t)i.code));
  X86CompilerFuncCall* call = c.call(global_exports_.XeTraceInstruction);
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder3<void, void*, uint64_t, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);
  call->setArgument(2, arg2);

  for (int n = 0; n < 2; n++) {
    c.nop();
  }
}

void X64Emitter::TraceInvalidInstruction(InstrData& i) {
  X86Compiler& c = compiler_;
#if 0
  if (FLAGS_annotate_disassembly) {
    c.comment("XeInvalidInstruction (+spill)");
  }

  for (int n = 0; n < 5; n++) {
    c.nop();
  }

  SpillRegisters();

  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)i.address));
  GpVar arg2 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg2, imm((uint64_t)i.code));
  X86CompilerFuncCall* call = c.call(global_exports_.XeInvalidInstruction);
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder3<void, void*, uint64_t, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);
  call->setArgument(2, arg2);

  for (int n = 0; n < 5; n++) {
    c.nop();
  }
#endif
}

void X64Emitter::TraceBranch(uint32_t cia) {
  X86Compiler& c = compiler_;

  if (!FLAGS_trace_branches) {
    return;
  }

  for (int n = 0; n < 5; n++) {
    c.nop();
  }

  if (FLAGS_annotate_disassembly) {
    c.comment("XeTraceBranch (+spill)");
  }

  SpillRegisters();

  // Pick target. If it's an indirection the tracing function will handle it.
  uint64_t target = 0;
  switch (fn_block_->outgoing_type) {
    case FunctionBlock::kTargetBlock:
      target = fn_block_->outgoing_address;
      break;
    case FunctionBlock::kTargetFunction:
      target = fn_block_->outgoing_function->start_address;
      break;
    case FunctionBlock::kTargetLR:
      target = kXEPPCRegLR;
      break;
    case FunctionBlock::kTargetCTR:
      target = kXEPPCRegCTR;
      break;
    default:
    case FunctionBlock::kTargetNone:
      XEASSERTALWAYS();
      break;
  }

  // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
  GpVar arg1 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg1, imm((uint64_t)cia));
  GpVar arg2 = c.newGpVar(kX86VarTypeGpq);
  c.mov(arg2, imm((uint64_t)target));
  X86CompilerFuncCall* call = c.call(global_exports_.XeTraceBranch);
  call->setComment("XeTraceBranch");
  call->setPrototype(kX86FuncConvDefault,
      FuncBuilder3<void, void*, uint64_t, uint64_t>());
  call->setArgument(0, c.getGpArg(0));
  call->setArgument(1, arg1);
  call->setArgument(2, arg2);

  for (int n = 0; n < 2; n++) {
    c.nop();
  }
}

int X64Emitter::GenerateIndirectionBranch(uint32_t cia, GpVar& target,
                                             bool lk, bool likely_local) {
  X86Compiler& c = compiler_;

  // This function is called by the control emitters when they know that an
  // indirect branch is required.
  // It first tries to see if the branch is to an address within the function
  // and, if so, uses a local switch table. If that fails because we don't know
  // the block the function is regenerated (ACK!). If the target is external
  // then an external call occurs.

  // Request builds of the indirection blocks on demand.
  // We can't build here because we don't know what registers will be needed
  // yet, so we just create the blocks and let GenerateSharedBlocks handle it
  // after we are done with all user instructions.
  if ((likely_local || !lk) &&
      external_indirection_block_.getId() == kInvalidValue) {
    external_indirection_block_ = c.newLabel();
  }
  if (likely_local && internal_indirection_block_.getId() == kInvalidValue) {
    internal_indirection_block_ = c.newLabel();
  }
  if ((internal_indirection_block_.getId() != kInvalidValue ||
       external_indirection_block_.getId() != kInvalidValue) &&
      (locals_.indirection_cia.getId() == kInvalidValue)) {
    locals_.indirection_target = c.newGpVar();
    locals_.indirection_cia = c.newGpVar();
  }

  // Check to see if the target address is within the function.
  // If it is jump to that basic block. If the basic block is not found it means
  // we have a jump inside the function that wasn't identified via static
  // analysis. These are bad as they require function regeneration.
  if (likely_local) {
    // Note that we only support LK=0, as we are using shared tables.
    XEASSERT(!lk);
    c.mov(locals_.indirection_target, target);
    c.mov(locals_.indirection_cia, imm(cia));
    // if (target >= start && target < end) jmp internal_indirection_block;
    // else jmp external_indirection_block;
    GpVar in_range(c.newGpVar());
    c.cmp(target, imm(symbol_->start_address));
    c.setge(in_range.r8Lo());
    c.jl(external_indirection_block_, kCondHintLikely);
    c.cmp(target, imm(symbol_->end_address));
    c.jge(external_indirection_block_, kCondHintLikely);
    c.jmp(internal_indirection_block_);
    return 0;
  }

  // If we are LK=0 jump to the shared indirection block. This prevents us
  // from needing to fill the registers again after the call and shares more
  // code.
  if (!lk) {
    c.mov(locals_.indirection_target, target);
    c.mov(locals_.indirection_cia, imm(cia));
    c.jmp(external_indirection_block_);
  } else {
    // Slowest path - spill, call the external function, and fill.
    // We should avoid this at all costs.

    // Spill registers. We could probably share this.
    SpillRegisters();

    // Grab the target of the indirection.
    // TODO(benvanik): remove once fixed: https://code.google.com/p/asmjit/issues/detail?id=86
    GpVar arg2 = c.newGpVar(kX86VarTypeGpq);
    c.mov(arg2, imm(cia));
    X86CompilerFuncCall* call = c.call(global_exports_.XeIndirectBranch);
    call->setPrototype(kX86FuncConvDefault,
        FuncBuilder3<void*, void*, uint64_t, uint64_t>());
    call->setArgument(0, c.getGpArg(0));
    call->setArgument(1, target);
    call->setArgument(2, arg2);
    GpVar target_ptr(c.newGpVar());
    call->setReturn(target_ptr);

    // Call target.
    // void fn(ppc_state*, uint64_t)
    call = c.call(target_ptr);
    call->setComment("Indirection branch");
    call->setPrototype(kX86FuncConvDefault,
        FuncBuilder2<void, void*, uint64_t>());
    call->setArgument(0, c.getGpArg(0));
    call->setArgument(1, arg2);

    // TODO(benvanik): next_block/is_last_block/etc
    //if (next_block) {
      // Only refill if not a tail call.
      FillRegisters();
    //}
  }

  return 0;
}

void X64Emitter::SetupLocals() {
  X86Compiler& c = compiler_;

  if (!FLAGS_cache_registers) {
    return;
  }

  uint64_t spr_t = access_bits_.spr;
  if (spr_t & 0x3) {
    locals_.xer = c.newGpVar(kX86VarTypeGpq, "xer");
  }
  spr_t >>= 2;
  if (spr_t & 0x3) {
    locals_.lr = c.newGpVar(kX86VarTypeGpq, "lr");
  }
  spr_t >>= 2;
  if (spr_t & 0x3) {
    locals_.ctr = c.newGpVar(kX86VarTypeGpq, "ctr");
  }
  spr_t >>= 2;
  // TODO: FPCSR

  char name[8];

  uint64_t cr_t = access_bits_.cr;
  for (int n = 0; n < 8; n++) {
    if (cr_t & 3) {
      xesnprintfa(name, XECOUNT(name), "cr%d", n);
      locals_.cr[n] = c.newGpVar(kX86VarTypeGpd, name);
    }
    cr_t >>= 2;
  }

  uint64_t gpr_t = access_bits_.gpr;
  for (int n = 0; n < 32; n++) {
    if (gpr_t & 3) {
      xesnprintfa(name, XECOUNT(name), "r%d", n);
      locals_.gpr[n] = c.newGpVar(kX86VarTypeGpq, name);
    }
    gpr_t >>= 2;
  }

  uint64_t fpr_t = access_bits_.fpr;
  for (int n = 0; n < 32; n++) {
    if (fpr_t & 3) {
      xesnprintfa(name, XECOUNT(name), "f%d", n);
      locals_.fpr[n] = c.newGpVar(kX86VarTypeXmmSD, name);
    }
    fpr_t >>= 2;
  }
}

void X64Emitter::FillRegisters() {
  X86Compiler& c = compiler_;

  if (!FLAGS_cache_registers) {
    return;
  }

  // This updates all of the local register values from the state memory.
  // It should be called on function entry for initial setup and after any
  // calls that may modify the registers.

  // TODO(benvanik): use access flags to see if we need to do reads/writes.

  if (locals_.xer.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Filling XER");
    }
    c.mov(locals_.xer,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, xer)));
  }

  if (locals_.lr.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Filling LR");
    }
    c.mov(locals_.lr,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, lr)));
  }

  if (locals_.ctr.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Filling CTR");
    }
    c.mov(locals_.ctr,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, ctr)));
  }

  // Fill the split CR values by extracting each one from the CR.
  // This could probably be done faster via an extractvalues or something.
  // Perhaps we could also change it to be a vector<8*i8>.
  GpVar cr;
  GpVar cr_tmp;
  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
    GpVar& cr_n = locals_.cr[n];
    if (cr_n.getId() == kInvalidValue) {
      continue;
    }
    if (cr.getId() == kInvalidValue) {
      // Only fetch once. Doing this in here prevents us from having to
      // always fetch even if unused.
      if (FLAGS_annotate_disassembly) {
        c.comment("Filling CR");
      }
      cr = c.newGpVar();
      c.mov(cr, qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, cr)));
      cr_tmp = c.newGpVar();
    }
    // (cr >> 28 - n * 4) & 0xF
    c.mov(cr_tmp, cr);
    if (n < 7) {
      c.shr(cr_tmp, imm(28 - n * 4));
    }
    c.and_(cr_tmp, imm(0xF));
    c.mov(cr_n, cr_tmp);
  }

  for (size_t n = 0; n < XECOUNT(locals_.gpr); n++) {
    if (locals_.gpr[n].getId() != kInvalidValue) {
      if (FLAGS_annotate_disassembly) {
        c.comment("Filling r%d", n);
      }
      c.mov(locals_.gpr[n],
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, r) + 8 * n));
    }
  }

  for (size_t n = 0; n < XECOUNT(locals_.fpr); n++) {
    if (locals_.fpr[n].getId() != kInvalidValue) {
      if (FLAGS_annotate_disassembly) {
        c.comment("Filling f%d", n);
      }
      c.mov(locals_.fpr[n],
            qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, f) + 8 * n));
    }
  }
}

void X64Emitter::SpillRegisters() {
  X86Compiler& c = compiler_;

  if (!FLAGS_cache_registers) {
    return;
  }

  // This flushes all local registers (if written) to the register bank and
  // resets their values.

  // TODO(benvanik): only flush if actually required, or selective flushes.

  if (locals_.xer.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Spilling XER");
    }
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, xer)),
          locals_.xer);
  }

  if (locals_.lr.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Spilling LR");
    }
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, lr)),
          locals_.lr);
  }

  if (locals_.ctr.getId() != kInvalidValue) {
    if (FLAGS_annotate_disassembly) {
      c.comment("Spilling CTR");
    }
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, ctr)),
          locals_.ctr);
  }

  // Stitch together all split CR values.
  // TODO(benvanik): don't flush across calls?
  GpVar cr;
  GpVar cr_tmp;
  for (size_t n = 0; n < XECOUNT(locals_.cr); n++) {
    GpVar& cr_n = locals_.cr[n];
    if (cr_n.getId() == kInvalidValue) {
      continue;
    }
    if (cr_tmp.getId() == kInvalidValue) {
      cr_tmp = c.newGpVar();
      if (FLAGS_annotate_disassembly) {
        c.comment("Spilling CR");
      }
    }
    // cr |= (cr_n << n * 4)
    c.mov(cr_tmp, cr_n);
    if (n) {
      c.shl(cr_tmp, imm(n * 4));
    }
    if (cr.getId() == kInvalidValue) {
      cr = c.newGpVar();
      c.mov(cr, cr_tmp);
    } else {
      c.or_(cr, cr_tmp);
    }
  }
  if (cr.getId() != kInvalidValue) {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, cr)),
          cr);
  }

  for (uint32_t n = 0; n < XECOUNT(locals_.gpr); n++) {
    GpVar& v = locals_.gpr[n];
    if (v.getId() != kInvalidValue) {
      if (FLAGS_annotate_disassembly) {
        c.comment("Spilling r%d", n);
      }
      c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, r) + 8 * n),
            v);
    }
  }

  for (uint32_t n = 0; n < XECOUNT(locals_.fpr); n++) {
    GpVar& v = locals_.fpr[n];
    if (v.getId() != kInvalidValue) {
      if (FLAGS_annotate_disassembly) {
        c.comment("Spilling f%d", n);
      }
      c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, f) + 8 * n),
            v);
    }
  }
}

GpVar X64Emitter::xer_value() {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.xer.getId() != kInvalidValue);
    return locals_.xer;
  } else {
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, xer)));
    return value;
  }
}

void X64Emitter::update_xer_value(GpVar& value) {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.xer.getId() != kInvalidValue);
    c.mov(locals_.xer, zero_extend(value, 0, 8));
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, xer)),
          zero_extend(value, 0, 8));
  }
}

void X64Emitter::update_xer_with_overflow(GpVar& value) {
  X86Compiler& c = compiler_;
  GpVar xer(c.newGpVar());
  c.mov(xer, xer_value());
  c.and_(xer, imm(0xBFFFFFFF)); // clear bit 30
  c.and_(value, imm(1));
  c.shl(value, imm(30));
  c.or_(xer, value);
  c.shl(value, imm(31)); // also stick value
  c.or_(xer, value);
  update_xer_value(xer);
}

// Set with a byte value indicating carry.
void X64Emitter::update_xer_with_carry(GpVar& value) {
  X86Compiler& c = compiler_;
  GpVar xer(c.newGpVar());
  c.mov(xer, xer_value());
  c.and_(xer, imm(0xDFFFFFFF)); // clear bit 29
  c.and_(value, imm(1));
  c.shl(value, imm(29));
  c.or_(xer, value);
  update_xer_value(xer);
}

#if 0
void X64Emitter::update_xer_with_overflow_and_carry(GpVar& value) {
  X86Compiler& c = compiler_;
  XEASSERT(locals_.xer.getId() != kInvalidValue);

  // Expects a i1 indicating overflow.
  // Trust the caller that if it's larger than that it's already truncated.
  value = zero_extend(value, 1, 8);

  // This is effectively an update_xer_with_overflow followed by an
  // update_xer_with_carry, but since the logic is largely the same share it.
  GpVar& xer = xer_value();
  // clear bit 30 & 29
  xer = jit_insn_and(fn_, xer, get_uint64(0xFFFFFFFF9FFFFFFF));
  xer = jit_insn_or(fn_, xer, jit_insn_shl(fn_, value, get_uint32(31)));
  xer = jit_insn_or(fn_, xer, jit_insn_shl(fn_, value, get_uint32(30)));
  xer = jit_insn_or(fn_, xer, jit_insn_shl(fn_, value, get_uint32(29)));
  jit_insn_store(fn_, locals_.xer, value);
}
#endif

GpVar X64Emitter::lr_value() {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.lr.getId() != kInvalidValue);
    return locals_.lr;
  } else {
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, lr)));
    return value;
  }
}

void X64Emitter::update_lr_value(GpVar& value) {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.lr.getId() != kInvalidValue);
    c.mov(locals_.lr, zero_extend(value, 0, 8));
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, lr)),
          zero_extend(value, 0, 8));
  }
}

void X64Emitter::update_lr_value(AsmJit::Imm& imm) {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.lr.getId() != kInvalidValue);
    c.mov(locals_.lr, imm);
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, lr)),
          imm);
  }
}

GpVar X64Emitter::ctr_value() {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.ctr.getId() != kInvalidValue);
    return locals_.ctr;
  } else {
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, ctr)));
    return value;
  }
}

void X64Emitter::update_ctr_value(GpVar& value) {
  X86Compiler& c = compiler_;
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.ctr.getId() != kInvalidValue);
    c.mov(locals_.ctr, zero_extend(value, 0, 8));
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, ctr)),
          zero_extend(value, 0, 8));
  }
}

GpVar X64Emitter::cr_value(uint32_t n) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 8);
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.cr[n].getId() != kInvalidValue);
    return locals_.cr[n];
  } else {
    // TODO(benvanik): this can most definitely be made more efficient.
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, cr)));
    if (n < 7) {
      c.shr(value, imm(28 - n * 4));
    }
    c.and_(value, imm(0xF));
    return value;
  }
}

void X64Emitter::update_cr_value(uint32_t n, GpVar& value) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 8);
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.cr[n].getId() != kInvalidValue);
    c.mov(locals_.cr[n], trunc(value, 1));
  } else {
    // TODO(benvanik): this can most definitely be made more efficient.
    GpVar cr_tmp(c.newGpVar());
    c.mov(cr_tmp, qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, cr)));
    GpVar cr_n(c.newGpVar());
    c.mov(cr_n, value);
    c.and_(cr_n, imm(0xF));
    if (n < 7) {
      c.shl(cr_n, imm(28 - n * 4));
    }
    c.and_(cr_tmp, imm(~(0xF << (28 - n * 4))));
    c.or_(cr_tmp, cr_n);
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, cr)), cr_tmp);
  }
}

void X64Emitter::update_cr_with_cond(uint32_t n, GpVar& lhs) {
  X86Compiler& c = compiler_;
  // bit0 = RA < 0
  // bit1 = RA > 0
  // bit2 = RA = 0
  // bit3 = XER[SO]

  // Compare and set bits.
  GpVar v_l(c.newGpVar());
  GpVar v_g(c.newGpVar());
  GpVar v_e(c.newGpVar());
  c.cmp(lhs, imm(0));
  c.setl(v_l.r8());
  c.setg(v_g.r8());
  c.sete(v_e.r8());
  GpVar v(c.newGpVar());
  c.shl(v_g, imm(1));
  c.shl(v_e, imm(2));
  c.mov(v, v_l.r8());
  c.or_(v, v_g.r8());
  c.or_(v, v_e.r8());

  // TODO(benvanik): set bit 4 to XER[SO]
  // c.seto?

  // Insert the 4 bits into their location in the CR.
  update_cr_value(n, v);
}

void X64Emitter::update_cr_with_cond(uint32_t n, GpVar& lhs, GpVar& rhs) {
  X86Compiler& c = compiler_;
  // bit0 = RA < RB
  // bit1 = RA > RB
  // bit2 = RA = RB
  // bit3 = XER[SO]

  // Compare and set bits.
  GpVar v_l(c.newGpVar());
  GpVar v_g(c.newGpVar());
  GpVar v_e(c.newGpVar());
  c.cmp(lhs, rhs);
  c.setl(v_l.r8());
  c.setg(v_g.r8());
  c.sete(v_e.r8());
  GpVar v(c.newGpVar());
  c.shl(v_g, imm(1));
  c.shl(v_e, imm(2));
  c.mov(v, v_l.r8());
  c.or_(v, v_g.r8());
  c.or_(v, v_e.r8());

  // TODO(benvanik): set bit 4 to XER[SO]
  // c.seto?

  // Insert the 4 bits into their location in the CR.
  update_cr_value(n, v);
}

GpVar X64Emitter::gpr_value(uint32_t n) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 32);

  // Actually r0 is writable, even though nobody should ever do that.
  // Perhaps we can check usage and enable this if safe?
  // if (n == 0) {
  //   return get_uint64(0);
  // }

  if (FLAGS_cache_registers) {
    XEASSERT(locals_.gpr[n].getId() != kInvalidValue);
    return locals_.gpr[n];
  } else {
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, r) + 8 * n));
    return value;
  }
}

void X64Emitter::update_gpr_value(uint32_t n, GpVar& value) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 32);
  // See above - r0 can be written.
  // if (n == 0) {
  //   // Ignore writes to zero.
  //   return;
  // }

  if (FLAGS_cache_registers) {
    XEASSERT(locals_.gpr[n].getId() != kInvalidValue);
    c.mov(locals_.gpr[n], zero_extend(value, 0, 8));
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, r) + 8 * n),
          zero_extend(value, 0, 8));
  }
}

GpVar X64Emitter::fpr_value(uint32_t n) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 32);
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.fpr[n].getId() != kInvalidValue);
    return locals_.fpr[n];
  } else {
    GpVar value(c.newGpVar());
    c.mov(value,
          qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, f) + 8 * n));
    return value;
  }
}

void X64Emitter::update_fpr_value(uint32_t n, GpVar& value) {
  X86Compiler& c = compiler_;
  XEASSERT(n >= 0 && n < 32);
  if (FLAGS_cache_registers) {
    XEASSERT(locals_.fpr[n].getId() != kInvalidValue);
    c.mov(locals_.fpr[n], value);
  } else {
    c.mov(qword_ptr(c.getGpArg(0), offsetof(xe_ppc_state_t, f) + 8 * n),
          value);
  }
}

GpVar X64Emitter::TouchMemoryAddress(uint32_t cia, GpVar& addr) {
  X86Compiler& c = compiler_;

  // Input address is always in 32-bit space.
  GpVar real_address(c.newGpVar());
  c.mov(real_address.r32(), addr.r32());

  // Add runtime memory address checks, if needed.
  if (FLAGS_memory_address_verification) {
  //   BasicBlock* invalid_bb = BasicBlock::Create(*context_, "", fn_);
  //   BasicBlock* valid_bb = BasicBlock::Create(*context_, "", fn_);

  //   // The heap starts at 0x1000 - if we write below that we're boned.
  //   jit_value_t gt = b.CreateICmpUGE(addr, b.getInt64(0x00001000));
  //   b.CreateCondBr(gt, valid_bb, invalid_bb);

  //   b.SetInsertPoint(invalid_bb);
  //   jit_value_t access_violation = gen_module_->getFunction("XeAccessViolation");
  //   SpillRegisters();
  //   b.CreateCall3(access_violation,
  //                 fn_->arg_begin(),
  //                 b.getInt32(cia),
  //                 addr);
  //   b.CreateBr(valid_bb);

  //   b.SetInsertPoint(valid_bb);
  }

  // Rebase off of memory pointer.
  uint64_t membase = (uint64_t)xe_memory_addr(memory_, 0);
  c.add(real_address, get_uint64(membase));
  return real_address;
}

GpVar X64Emitter::ReadMemory(
    uint32_t cia, GpVar& addr, uint32_t size, bool acquire) {
  X86Compiler& c = compiler_;

  // Rebase off of memory base pointer.
  GpVar real_address = TouchMemoryAddress(cia, addr);

  if (acquire) {
    // TODO(benvanik): acquire semantics.
    // load_value->setAlignment(size);
    // load_value->setVolatile(true);
    // load_value->setAtomic(Acquire);
    XELOGE("Ignoring acquire semantics on read -- TODO");
  }

  GpVar value(c.newGpVar());
  bool needs_swap = false;
  switch (size) {
    case 1:
      c.mov(value.r8(), byte_ptr(real_address));
      c.and_(value, imm(0xFF));
      break;
    case 2:
      c.mov(value.r16(), word_ptr(real_address));
      c.and_(value, imm(0xFFFF));
      c.xchg(value.r8Lo(), value.r8Hi());
      break;
    case 4:
      c.mov(value.r32(), dword_ptr(real_address));
      // No need to and -- the mov to e*x will extend for us.
      c.bswap(value.r32());
      break;
    case 8:
      c.mov(value, qword_ptr(real_address));
      c.bswap(value.r64());
      break;
    default:
      XEASSERTALWAYS();
      c.mov(value, imm(0xDEADBEEF));
      break;
  }

  return value;
}

void X64Emitter::WriteMemory(
    uint32_t cia, GpVar& addr, uint32_t size, GpVar& value,
    bool release) {
  X86Compiler& c = compiler_;

  // Rebase off of memory base pointer.
  GpVar real_address = TouchMemoryAddress(cia, addr);

  GpVar tmp;
  switch (size) {
    case 1:
      c.mov(byte_ptr(real_address), value.r8());
      break;
    case 2:
      tmp = c.newGpVar();
      c.mov(tmp, value);
      c.xchg(tmp.r8Lo(), tmp.r8Hi());
      c.mov(word_ptr(real_address), tmp.r16());
      break;
    case 4:
      tmp = c.newGpVar();
      c.mov(tmp, value);
      c.bswap(tmp.r32());
      c.mov(dword_ptr(real_address), tmp.r32());
      break;
    case 8:
      tmp = c.newGpVar();
      c.mov(tmp, value);
      c.bswap(tmp.r64());
      c.mov(qword_ptr(real_address), tmp.r64());
      break;
    default:
      XEASSERTALWAYS();
      return;
  }

  // TODO(benvanik): release semantics
  if (release) {
  //   store_value->setAlignment(size);
  //   store_value->setVolatile(true);
  //   store_value->setAtomic(Release);
    XELOGE("Ignoring release semantics on write -- TODO");
  }
}

GpVar X64Emitter::get_uint64(uint64_t value) {
  X86Compiler& c = compiler_;
  GpVar v(c.newGpVar());
  c.mov(v, imm(value));
  return v;
}

GpVar X64Emitter::sign_extend(GpVar& value, int from_size, int to_size) {
  X86Compiler& c = compiler_;

  if (!from_size) {
    from_size = value.getSize();
  }

  // No-op if the same size.
  if (from_size == to_size) {
    return value;
  }

  // TODO(benvanik): use movsx if value is in memory.

  // errrr.... could pin values to rax for cbw/cwde/cdqe
  // or, could use shift trick (may be slower):
  //   shlq $(target_len-src_len), reg
  //   sarq $(target_len-src_len), reg
  GpVar tmp(c.newGpVar());
  switch (from_size) {
  case 1:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2:
      c.mov(tmp, value);
      c.cbw(tmp);     // b->w
      return tmp;
    case 4:
      c.mov(tmp, value);
      c.cbw(tmp);     // b->w
      c.cwde(tmp);    // w->d
      return tmp;
    case 8:
      c.mov(tmp, value);
      c.cbw(tmp);     // b->w
      c.cwde(tmp);    // w->d
      c.cdqe(tmp);    // d->q
      return tmp;
    }
    break;
  case 2:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2: XEASSERTALWAYS(); return value;
    case 4:
      c.mov(tmp, value);
      c.cwde(tmp);    // w->d
      return tmp;
    case 8:
      c.mov(tmp, value);
      c.cwde(tmp);    // w->d
      c.cdqe(tmp);    // d->q
      return tmp;
    }
    break;
  case 4:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2: XEASSERTALWAYS(); return value;
    case 4: XEASSERTALWAYS(); return value;
    case 8:
      c.mov(tmp, value);
      c.cdqe(tmp);    // d->q
      return tmp;
    }
    break;
  case 8: break;
  }
  XEASSERTALWAYS();
  return value;
}

GpVar X64Emitter::zero_extend(GpVar& value, int from_size, int to_size) {
  X86Compiler& c = compiler_;

  if (!from_size) {
    from_size = value.getSize();
  }

  // No-op if the same size.
  if (from_size == to_size) {
    return value;
  }

  // TODO(benvanik): use movzx if value is in memory.

  GpVar tmp(c.newGpVar());
  switch (from_size) {
  case 1:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2:
      c.mov(tmp, value.r8());
      return tmp.r16();
    case 4:
      c.mov(tmp, value.r8());
      return tmp.r32();
    case 8:
      c.mov(tmp, value.r8());
      return tmp.r64();
    }
    break;
  case 2:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2: XEASSERTALWAYS(); return value;
    case 4:
      c.mov(tmp, value.r16());
      return tmp.r32();
    case 8:
      c.mov(tmp, value.r16());
      return tmp.r64();
    }
    break;
  case 4:
    switch (to_size) {
    case 1: XEASSERTALWAYS(); return value;
    case 2: XEASSERTALWAYS(); return value;
    case 4: XEASSERTALWAYS(); return value;
    case 8:
      c.mov(tmp, value.r32());
      return tmp.r64();
    }
    break;
  case 8: break;
  }
  XEASSERTALWAYS();
  return value;
}

GpVar X64Emitter::trunc(GpVar& value, int size) {
  X86Compiler& c = compiler_;
  XEASSERTALWAYS();
  return value;
#if 0
  // No-op if the same size.
  if (value.getSize() == size) {
    return value;
  }

  switch (size) {
  case 1:
    return value.r8();
  case 2:
    return value.r16();
  case 4:
    return value.r32();
  default:
  case 8:
    return value.r64();
  }
#endif
}
