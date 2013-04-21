/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvmbe/llvm_code_unit_builder.h>

#include <llvm/DIBuilder.h>
#include <llvm/Linker.h>
#include <llvm/PassManager.h>
#include <llvm/DebugInfo.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/llvmbe/emitter_context.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::llvmbe;
using namespace xe::cpu::sdb;


LLVMCodeUnitBuilder::LLVMCodeUnitBuilder(
    xe_memory_ref memory, LLVMContext* context) :
    CodeUnitBuilder(memory),
    context_(context) {
}

LLVMCodeUnitBuilder::~LLVMCodeUnitBuilder() {
  Reset();
}

Module* LLVMCodeUnitBuilder::module() {
  return module_;
}

int LLVMCodeUnitBuilder::Init(const char* module_name,
                              const char* module_path) {
  Reset();

  // Create LLVM module.
  module_ = new Module(module_name, *context_);

  // Create the emitter used to generate functions.
  emitter_ = new EmitterContext(memory_, context_, module_);

  // Setup optimization pass.
  fpm_ = new FunctionPassManager(module_);
  if (FLAGS_optimize_ir_functions) {
    PassManagerBuilder pmb;
    pmb.OptLevel      = 3;
    pmb.SizeLevel     = 0;
    pmb.Inliner       = createFunctionInliningPass();
    pmb.Vectorize     = true;
    pmb.LoopVectorize = true;
    pmb.populateFunctionPassManager(*fpm_);
  }
  // TODO(benvanik): disable verifier in release builds?
  fpm_->add(createVerifierPass());

  // Setup a debug info builder.
  // This is used when creating any debug info. We may want to go more
  // fine grained than this, but for now it's something.
  char dir[XE_MAX_PATH];
  XEIGNORE(xestrcpya(dir, XECOUNT(dir), module_path));
  char* slash = xestrrchra(dir, '/');
  if (slash) {
    *(slash + 1) = 0;
  }
  di_builder_ = new DIBuilder(*module_);
  di_builder_->createCompileUnit(
      dwarf::DW_LANG_C99, //0x8010,
      StringRef(module_name),
      StringRef(dir),
      StringRef("xenia"),
      true,
      StringRef(""),
      0);
  cu_ = (MDNode*)di_builder_->getCU();

  return 0;
}

int LLVMCodeUnitBuilder::MakeFunction(FunctionSymbol* symbol) {
  return MakeFunction(symbol, NULL);
}

int LLVMCodeUnitBuilder::MakeFunction(FunctionSymbol* symbol,
                                      Function** out_fn) {
  int result_code = 0;

  // Create the function (and setup args/attributes/etc).
  Function* fn = emitter_->GetFunction(symbol);

  // If already defined, ignore.
  if (!fn->isDeclaration()) {
    if (out_fn) {
      *out_fn = fn;
    }
    return 0;
  }

  switch (symbol->type) {
  case FunctionSymbol::User:
    result_code = MakeUserFunction(symbol, fn);
    break;
  case FunctionSymbol::Kernel:
    if (symbol->kernel_export && symbol->kernel_export->is_implemented) {
      result_code = MakePresentImportFunction(symbol, fn);
    } else {
      result_code = MakeMissingImportFunction(symbol, fn);
    }
    break;
  default:
    XEASSERTALWAYS();
    return 1;
  }
  if (result_code) {
    return result_code;
  }

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  OptimizeFunction(fn);

  // Add to map for processing later.
  functions_.insert(std::pair<uint32_t, Function*>(symbol->start_address, fn));

  if (out_fn) {
    *out_fn = fn;
  }

  return 0;
}

int LLVMCodeUnitBuilder::MakeUserFunction(FunctionSymbol* symbol,
                                          Function* fn) {
  // Setup emitter.
  emitter_->Init(symbol, fn);

  // Emit.
  emitter_->GenerateBasicBlocks();

  return 0;
}

int LLVMCodeUnitBuilder::MakePresentImportFunction(FunctionSymbol* symbol,
                                                   Function* fn) {
  LLVMContext& context = *context_;

  // Pick names.
  // We have both the shim function pointer and the shim data pointer.
  char shim_name[256];
  xesnprintfa(shim_name, XECOUNT(shim_name),
              "__shim_%s", symbol->kernel_export->name);
  char shim_data_name[256];
  xesnprintfa(shim_data_name, XECOUNT(shim_data_name),
              "__shim_data_%s", symbol->kernel_export->name);

  // Hardcoded to 64bits.
  Type* intPtrTy = IntegerType::get(context, 64);
  Type* int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(context));

  // Declare shim function.
  std::vector<Type*> shimArgs;
  shimArgs.push_back(int8PtrTy);
  shimArgs.push_back(int8PtrTy);
  FunctionType* shimTy = FunctionType::get(
      Type::getVoidTy(context), shimArgs, false);
  Function* shim = Function::Create(
      shimTy, Function::ExternalLinkage, shim_name, module_);

  GlobalVariable* shim_data = new GlobalVariable(
      *module_, int8PtrTy, false, GlobalValue::ExternalLinkage, 0,
      shim_data_name);
  shim_data->setInitializer(ConstantExpr::getIntToPtr(
      ConstantInt::get(intPtrTy, 0), int8PtrTy));

  BasicBlock* block = BasicBlock::Create(context, "entry", fn);
  IRBuilder<> b(block);

  if (FLAGS_trace_kernel_calls) {
    Value* traceKernelCall = module_->getFunction("XeTraceKernelCall");
    b.CreateCall4(
        traceKernelCall,
        fn->arg_begin(),
        b.getInt64(symbol->start_address),
        ++fn->arg_begin(),
        b.getInt64((uint64_t)symbol->kernel_export));
  }

  b.CreateCall2(
      shim,
      fn->arg_begin(),
      b.CreateLoad(shim_data));

  b.CreateRetVoid();

  return 0;
}

int LLVMCodeUnitBuilder::MakeMissingImportFunction(FunctionSymbol* symbol,
                                                   Function* fn) {
  BasicBlock* block = BasicBlock::Create(*context_, "entry", fn);
  IRBuilder<> b(block);

  if (FLAGS_trace_kernel_calls) {
    Value* traceKernelCall = module_->getFunction("XeTraceKernelCall");
    b.CreateCall4(
        traceKernelCall,
        fn->arg_begin(),
        b.getInt64(symbol->start_address),
        ++fn->arg_begin(),
        b.getInt64((uint64_t)symbol->kernel_export));
  }

  b.CreateRetVoid();

  return 0;
}

void LLVMCodeUnitBuilder::OptimizeFunction(Function* fn) {
  //fn->dump();
  fpm_->run(*fn);
  fn->dump();
}

int LLVMCodeUnitBuilder::Finalize() {
  // Finalize debug info.
  di_builder_->finalize();

  // TODO(benvanik): run module optimizations, if enabled.

  return 0;
}

void LLVMCodeUnitBuilder::Reset() {
  functions_.clear();

  delete emitter_;
  emitter_ = NULL;
  delete di_builder_;
  di_builder_ = NULL;
  delete fpm_;
  fpm_ = NULL;
  delete module_;
  module_ = NULL;
}
