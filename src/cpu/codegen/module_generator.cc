/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/codegen/module_generator.h>

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

#include <xenia/cpu/ppc.h>
#include <xenia/cpu/codegen/function_generator.h>

#include "cpu/cpu-private.h"


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::codegen;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


ModuleGenerator::ModuleGenerator(
    xe_memory_ref memory, ExportResolver* export_resolver,
    const char* module_name, const char* module_path, SymbolDatabase* sdb,
    LLVMContext* context, Module* gen_module, ExecutionEngine* engine) {
  memory_ = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
  module_name_ = xestrdupa(module_name);
  module_path_ = xestrdupa(module_path);
  sdb_ = sdb;
  context_ = context;
  gen_module_ = gen_module;
  engine_ = engine;
  di_builder_ = NULL;
}

ModuleGenerator::~ModuleGenerator() {
  for (std::map<uint32_t, CodegenFunction*>::iterator it =
       functions_.begin(); it != functions_.end(); ++it) {
    delete it->second;
  }

  delete di_builder_;
  xe_free(module_path_);
  xe_free(module_name_);
  xe_memory_release(memory_);
}

int ModuleGenerator::Generate() {
  std::string error_message;

  // Setup a debug info builder.
  // This is used when creating any debug info. We may want to go more
  // fine grained than this, but for now it's something.
  xechar_t dir[2048];
  XEIGNORE(xestrcpy(dir, XECOUNT(dir), module_path_));
  xechar_t* slash = xestrrchr(dir, '/');
  if (slash) {
    *(slash + 1) = 0;
  }
  di_builder_ = new DIBuilder(*gen_module_);
  di_builder_->createCompileUnit(
      dwarf::DW_LANG_C99, //0x8010,
      StringRef(module_name_),
      StringRef(dir),
      StringRef("xenia"),
      true,
      StringRef(""),
      0);
  cu_ = (MDNode*)di_builder_->getCU();

  // Add export wrappers.
  //

  // Add all functions.
  // We do two passes - the first creates the function signature and global
  // value (so that we can call it), the second actually builds the function.
  std::vector<FunctionSymbol*> functions;
  if (!sdb_->GetAllFunctions(functions)) {
    for (std::vector<FunctionSymbol*>::iterator it = functions.begin();
         it != functions.end(); ++it) {
      FunctionSymbol* fn = *it;
      switch (fn->type) {
      case FunctionSymbol::User:
        PrepareFunction(fn);
        break;
      case FunctionSymbol::Kernel:
        if (fn->kernel_export && fn->kernel_export->is_implemented) {
          AddPresentImport(fn);
        } else {
          AddMissingImport(fn);
        }
        break;
      default:
        XEASSERTALWAYS();
        break;
      }
    }
  }

  // Build out all the user functions.
  for (std::map<uint32_t, CodegenFunction*>::iterator it =
       functions_.begin(); it != functions_.end(); ++it) {
    BuildFunction(it->second);
  }

  di_builder_->finalize();

  return 0;
}

void ModuleGenerator::AddFunctionsToMap(
    std::tr1::unordered_map<uint32_t, llvm::Function*>& map) {
  for (std::map<uint32_t, CodegenFunction*>::iterator it = functions_.begin();
       it != functions_.end(); ++it) {
    map.insert(std::pair<uint32_t, Function*>(it->first, it->second->function));
  }
}

ModuleGenerator::CodegenFunction* ModuleGenerator::GetCodegenFunction(
    uint32_t address) {
  std::map<uint32_t, CodegenFunction*>::iterator it = functions_.find(address);
  if (it != functions_.end()) {
    return it->second;
  }
  return NULL;
}

Function* ModuleGenerator::CreateFunctionDefinition(const char* name) {
  Module* m = gen_module_;
  LLVMContext& context = m->getContext();

  std::vector<Type*> args;
  args.push_back(PointerType::getUnqual(Type::getInt8Ty(context)));
  args.push_back(Type::getInt64Ty(context));
  Type* return_type = Type::getVoidTy(context);

  FunctionType* ft = FunctionType::get(return_type,
                                       ArrayRef<Type*>(args), false);
  Function* f = cast<Function>(m->getOrInsertFunction(
      StringRef(name), ft));
  f->setVisibility(GlobalValue::DefaultVisibility);

  // Indicate that the function will never be unwound with an exception.
  // If we ever support native exception handling we may need to remove this.
  f->doesNotThrow();

  // May be worth trying the X86_FastCall, as we only need state in a register.
  //f->setCallingConv(CallingConv::Fast);
  f->setCallingConv(CallingConv::C);

  Function::arg_iterator fn_args = f->arg_begin();
  // 'state'
  Value* fn_arg = fn_args++;
  fn_arg->setName("state");
  f->setDoesNotAlias(1);
  f->setDoesNotCapture(1);
  // 'state' should try to be in a register, if possible.
  // TODO(benvanik): verify that's a good idea.
  // f->getArgumentList().begin()->addAttr(
  //     Attribute::get(context, AttrBuilder().addAttribute(Attribute::InReg)));

  // 'lr'
  fn_arg = fn_args++;
  fn_arg->setName("lr");

  return f;
};

void ModuleGenerator::AddMissingImport(FunctionSymbol* fn) {
  Module *m = gen_module_;
  LLVMContext& context = m->getContext();

  // Create the function (and setup args/attributes/etc).
  Function* f = CreateFunctionDefinition(fn->name);

  BasicBlock* block = BasicBlock::Create(context, "entry", f);
  IRBuilder<> b(block);

  if (FLAGS_trace_kernel_calls) {
    Value* traceKernelCall = m->getFunction("XeTraceKernelCall");
    b.CreateCall4(
        traceKernelCall,
        f->arg_begin(),
        b.getInt64(fn->start_address),
        ++f->arg_begin(),
        b.getInt64((uint64_t)fn->kernel_export));
  }

  b.CreateRetVoid();

  OptimizeFunction(m, f);

  //GlobalAlias *alias = new GlobalAlias(f->getType(), GlobalValue::InternalLinkage, name, f, m);
  // printf("   F %.8X %.8X %.3X (%3d) %s %s\n",
  //        info->value_address, info->thunk_address, info->ordinal,
  //        info->ordinal, implemented ? "  " : "!!", name);
  // For values:
  // printf("   V %.8X          %.3X (%3d) %s %s\n",
  //        info->value_address, info->ordinal, info->ordinal,
  //        implemented ? "  " : "!!", name);
}

void ModuleGenerator::AddPresentImport(FunctionSymbol* fn) {
  Module *m = gen_module_;
  LLVMContext& context = m->getContext();

  const DataLayout* dl = engine_->getDataLayout();
  Type* intPtrTy = dl->getIntPtrType(context);
  Type* int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(context));

  // Add the externs.
  // We have both the shim function pointer and the shim data pointer.
  char shim_name[256];
  xesnprintfa(shim_name, XECOUNT(shim_name),
              "__shim_%s", fn->kernel_export->name);
  char shim_data_name[256];
  xesnprintfa(shim_data_name, XECOUNT(shim_data_name),
              "__shim_data_%s", fn->kernel_export->name);
  std::vector<Type*> shimArgs;
  shimArgs.push_back(int8PtrTy);
  shimArgs.push_back(int8PtrTy);
  FunctionType* shimTy = FunctionType::get(
      Type::getVoidTy(context), shimArgs, false);
  Function* shim = Function::Create(
      shimTy, Function::ExternalLinkage, shim_name, m);

  GlobalVariable* gv = new GlobalVariable(
      *m, int8PtrTy, true, GlobalValue::ExternalLinkage, 0,
      shim_data_name);

  // TODO(benvanik): don't initialize on startup - move to exec_module
  gv->setInitializer(ConstantExpr::getIntToPtr(
      ConstantInt::get(intPtrTy,
                       (uintptr_t)fn->kernel_export->function_data.shim_data),
      int8PtrTy));
  engine_->addGlobalMapping(shim,
      (void*)fn->kernel_export->function_data.shim);

  // Create the function (and setup args/attributes/etc).
  Function* f = CreateFunctionDefinition(fn->name);

  BasicBlock* block = BasicBlock::Create(context, "entry", f);
  IRBuilder<> b(block);

  if (FLAGS_trace_kernel_calls) {
    Value* traceKernelCall = m->getFunction("XeTraceKernelCall");
    b.CreateCall4(
        traceKernelCall,
        f->arg_begin(),
        b.getInt64(fn->start_address),
        ++f->arg_begin(),
        b.getInt64((uint64_t)fn->kernel_export));
  }

  b.CreateCall2(
      shim,
      f->arg_begin(),
      b.CreateLoad(gv));

  b.CreateRetVoid();

  OptimizeFunction(m, f);
}

void ModuleGenerator::PrepareFunction(FunctionSymbol* fn) {
  // Module* m = gen_module_;
  // LLVMContext& context = m->getContext();

  // Create the function (and setup args/attributes/etc).
  Function* f = CreateFunctionDefinition(fn->name);

  // Setup our codegen wrapper to keep all the pointers together.
  CodegenFunction* cgf = new CodegenFunction();
  cgf->symbol = fn;
  cgf->function_type = f->getFunctionType();
  cgf->function = f;
  functions_.insert(std::pair<uint32_t, CodegenFunction*>(
      fn->start_address, cgf));
}

void ModuleGenerator::BuildFunction(CodegenFunction* cgf) {
  FunctionSymbol* fn = cgf->symbol;

  printf("%s:\n", fn->name);

  // Setup the generation context.
  FunctionGenerator fgen(
      memory_, sdb_, fn, context_, gen_module_, cgf->function);

  // Run through and generate each basic block.
  fgen.GenerateBasicBlocks();

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  OptimizeFunction(gen_module_, cgf->function);
}

void ModuleGenerator::OptimizeFunction(Module* m, Function* fn) {
  FunctionPassManager pm(m);
  //fn->dump();
  if (FLAGS_optimize_ir_functions) {
    PassManagerBuilder pmb;
    pmb.OptLevel      = 3;
    pmb.SizeLevel     = 0;
    pmb.Inliner       = createFunctionInliningPass();
    pmb.Vectorize     = true;
    pmb.LoopVectorize = true;
    pmb.populateFunctionPassManager(pm);
  }
  pm.add(createVerifierPass());
  pm.run(*fn);
}
