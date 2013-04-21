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

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/ppc.h>
#include <xenia/cpu/codegen/function_generator.h>

  // Build out all the user functions.
  size_t n = 0;
  XELOGI("Beginning generation of %ld functions...", functions.size());
  for (std::map<uint32_t, CodegenFunction*>::iterator it =
       functions_.begin(); it != functions_.end(); ++it, ++n) {
    FunctionSymbol* symbol = it->second->symbol;
    XELOGI("Generating %ld/%ld %.8X %s",
           n, functions_.size(), symbol->start_address, symbol->name());
    BuildFunction(it->second);
  }
  XELOGI("Function generation complete");

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


void ModuleGenerator::AddMissingImport(FunctionSymbol* fn) {
  Module *m = gen_module_;
  LLVMContext& context = m->getContext();

  // Create the function (and setup args/attributes/etc).
  Function* f = CreateFunctionDefinition(fn->name());

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
  Function* f = CreateFunctionDefinition(fn->name());

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
