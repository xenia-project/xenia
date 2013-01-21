/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/codegen.h>

#include <llvm/DIBuilder.h>
#include <llvm/Linker.h>
#include <llvm/PassManager.h>
#include <llvm/DebugInfo.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/ppc.h>
#include "cpu/ppc/instr_context.h"

using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::codegen;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


CodegenContext::CodegenContext(
    xe_memory_ref memory, ExportResolver* export_resolver,
    UserModule* module, SymbolDatabase* sdb,
    LLVMContext* context, Module* gen_module) {
  memory_ = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
  module_ = module;
  sdb_ = sdb;
  context_ = context;
  gen_module_ = gen_module;
}

CodegenContext::~CodegenContext() {
  xe_memory_release(memory_);
}

int CodegenContext::GenerateModule() {
  std::string error_message;

  // Setup a debug info builder.
  // This is used when creating any debug info. We may want to go more
  // fine grained than this, but for now it's something.
  xechar_t dir[2048];
  XEIGNORE(xestrcpy(dir, XECOUNT(dir), module_->path()));
  xechar_t *slash = xestrrchr(dir, '/');
  if (slash) {
    *(slash + 1) = 0;
  }
  di_builder_ = new DIBuilder(*gen_module_);
  di_builder_->createCompileUnit(
      0,
      StringRef(module_->name()),
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
        if (fn->kernel_export && fn->kernel_export->IsImplemented()) {
          AddPresentImport(fn);
        } else {
          AddMissingImport(fn);
        }
        break;
      default:
        break;
      }
    }
  }
  for (std::map<uint32_t, CodegenFunction*>::iterator it =
       functions_.begin(); it != functions_.end(); ++it) {
    BuildFunction(it->second);
  }

  di_builder_->finalize();

  return 0;
}

CodegenFunction* CodegenContext::GetCodegenFunction(uint32_t address) {
  std::map<uint32_t, CodegenFunction*>::iterator it = functions_.find(address);
  if (it != functions_.end()) {
    return it->second;
  }
  return NULL;
}

void CodegenContext::AddMissingImport(FunctionSymbol* fn) {
  Module* m = gen_module_;
  LLVMContext& context = m->getContext();

  AttributeWithIndex awi[] = {
    //AttributeWithIndex::get(context, 2, Attributes::NoCapture),
    AttributeWithIndex::get(context,
        AttributeSet::FunctionIndex, Attribute::NoUnwind),
  };
  AttributeSet attrs = AttributeSet::get(context, awi);

  std::vector<Type*> args;
  Type* return_type = Type::getInt32Ty(context);

  FunctionType* ft = FunctionType::get(return_type,
                                       ArrayRef<Type*>(args), false);
  Function* f = cast<Function>(m->getOrInsertFunction(
      StringRef(fn->name), ft, attrs));
  f->setCallingConv(CallingConv::C);
  f->setVisibility(GlobalValue::DefaultVisibility);

  // TODO(benvanik): log errors.
  BasicBlock* block = BasicBlock::Create(context, "entry", f);
  IRBuilder<> builder(block);
  Value* tmp = builder.getInt32(0);
  builder.CreateRet(tmp);

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

void CodegenContext::AddPresentImport(FunctionSymbol* fn) {
  // Module *m = gen_module_;
  // LLVMContext& context = m->getContext();

  // TODO(benvanik): add import thunk code.
}

void CodegenContext::PrepareFunction(FunctionSymbol* fn) {
  Module* m = gen_module_;
  LLVMContext& context = m->getContext();

  AttributeWithIndex awi[] = {
    //AttributeWithIndex::get(context, 2, Attributes::NoCapture),
    AttributeWithIndex::get(context,
        AttributeSet::FunctionIndex, Attribute::NoUnwind),
  };
  AttributeSet attrs = AttributeSet::get(context, awi);

  std::vector<Type*> args;
  Type* return_type = Type::getInt32Ty(context);

  char name[64];
  char* pname = name;
  if (fn->name) {
    pname = fn->name;
  } else {
    xesnprintfa(name, XECOUNT(name), "sub_%.8X", fn->start_address);
  }

  FunctionType* ft = FunctionType::get(return_type,
                                       ArrayRef<Type*>(args), false);
  Function* f = cast<Function>(
      m->getOrInsertFunction(StringRef(pname), ft, attrs));
  f->setCallingConv(CallingConv::C);
  f->setVisibility(GlobalValue::DefaultVisibility);

  CodegenFunction* cgf = new CodegenFunction();
  cgf->symbol = fn;
  cgf->function_type = ft;
  cgf->function = f;
  functions_.insert(std::pair<uint32_t, CodegenFunction*>(
      fn->start_address, cgf));
}

void CodegenContext::BuildFunction(CodegenFunction* cgf) {
  FunctionSymbol* fn = cgf->symbol;

  printf("%s:\n", fn->name);

  // Setup the generation context.
  InstrContext ic(memory_, fn, context_, gen_module_, cgf->function);

  // Run through and generate each basic block.
  ic.GenerateBasicBlocks();

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  OptimizeFunction(gen_module_, cgf->function);
}

void CodegenContext::OptimizeFunction(Module* m, Function* fn) {
  FunctionPassManager pm(m);
  PassManagerBuilder pmb;
  pmb.OptLevel      = 3;
  pmb.SizeLevel     = 0;
  pmb.Inliner       = createFunctionInliningPass();
  pmb.Vectorize     = true;
  pmb.LoopVectorize = true;
  pmb.populateFunctionPassManager(pm);
  pm.add(createVerifierPass());
  pm.run(*fn);
}
