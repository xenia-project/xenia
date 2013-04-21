/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvmbe/llvm_jit.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <xenia/cpu/exec_module.h>
#include <xenia/cpu/llvmbe/llvm_code_unit_builder.h>
#include <xenia/cpu/llvmbe/llvm_exports.h>
#include <xenia/cpu/sdb.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::llvmbe;
using namespace xe::cpu::sdb;


LLVMJIT::LLVMJIT(xe_memory_ref memory, FunctionTable* fn_table) :
    JIT(memory, fn_table),
    context_(NULL), engine_(NULL), module_(NULL), cub_(NULL) {
}

LLVMJIT::~LLVMJIT() {
  delete cub_;
  if (engine_) {
    engine_->removeModule(module_);
  }
  delete engine_;
  delete context_;
}

//namespace {
//void* LazyFunctionCreator(const std::string& name) {
//  printf("lazy: %s", name.c_str());
//  return NULL;
//}
//}

int LLVMJIT::Setup() {
  int result_code = 1;
  std::string error_message;

  context_ = new LLVMContext();

  // Create a shared code unit builder used while JITing.
  // Since there's only one we'll need to lock when generating code.
  // In the future we could try to generate new code in separate CUBs and then
  // link into the main module under the lock.
  cub_ = new LLVMCodeUnitBuilder(memory_, context_);
  result_code = cub_->Init("jit", "");
  if (result_code) {
    return result_code;
  }
  module_ = cub_->module();

  EngineBuilder builder(module_);
  builder.setEngineKind(EngineKind::JIT);
  builder.setErrorStr(&error_message);
  builder.setOptLevel(CodeGenOpt::None);
  //builder.setOptLevel(CodeGenOpt::Aggressive);
  //builder.setJITMemoryManager(jmm);
  //builder.setTargetOptions();
  builder.setAllocateGVsWithCode(false);
  //builder.setUseMCJIT(true);

  engine_ = builder.create();
  XEEXPECTNOTNULL(engine_);
  //engine_->DisableSymbolSearching();
  //engine_->InstallLazyFunctionCreator(LazyFunctionCreator);

  XEEXPECTZERO(InjectGlobals());

  result_code = 0;
XECLEANUP:
  return result_code;
}

int LLVMJIT::InjectGlobals() {
  LLVMContext& context = *context_;
  const DataLayout* dl = engine_->getDataLayout();
  Type* intPtrTy = dl->getIntPtrType(context);
  Type* int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(context));
  GlobalVariable* gv;

  // xe_memory_base
  // This is the base void* pointer to the memory space.
  gv = new GlobalVariable(
      *module_,
      int8PtrTy,
      true,
      GlobalValue::ExternalLinkage,
      0,
      "xe_memory_base");
  // Align to 64b - this makes SSE faster.
  gv->setAlignment(64);
  gv->setInitializer(ConstantExpr::getIntToPtr(
      ConstantInt::get(intPtrTy, (uintptr_t)xe_memory_addr(memory_, 0)),
      int8PtrTy));

  // Setup global exports (the Xe* functions called by generated code).
  GlobalExports global_exports;
  cpu::GetGlobalExports(&global_exports);
  SetupLlvmExports(&global_exports, module_, dl, engine_);

  return 0;
}

int LLVMJIT::InitModule(ExecModule* module) {
  SymbolDatabase* sdb = module->sdb();
  Module* cub_module = cub_->module();

  // Setup all imports (as needed).
  std::vector<FunctionSymbol*> functions;
  if (sdb->GetAllFunctions(functions)) {
    return 1;
  }
  int result_code = 0;
  for (std::vector<FunctionSymbol*>::iterator it = functions.begin();
      it != functions.end(); ++it) {
    FunctionSymbol* symbol = *it;
    if (symbol->type == FunctionSymbol::Kernel) {
      // Generate the function.
      Function* fn = NULL;
      result_code = cub_->MakeFunction(symbol, &fn);
      if (result_code) {
        XELOGE("Unable to generate import %s", symbol->name());
        return result_code;
      }

      // Set global mappings for shim and data.
      char shim_name[256];
      xesnprintfa(shim_name, XECOUNT(shim_name),
                  "__shim_%s", symbol->kernel_export->name);
      Function* shim = cub_module->getFunction(shim_name);
      if (shim) {
        engine_->updateGlobalMapping(
            shim, (void*)symbol->kernel_export->function_data.shim);
      }
      char shim_data_name[256];
      xesnprintfa(shim_data_name, XECOUNT(shim_data_name),
                  "__shim_data_%s", symbol->kernel_export->name);
      GlobalVariable* shim_data = cub_module->getGlobalVariable(shim_data_name);
      if (shim_data) {
        engine_->updateGlobalMapping(
            shim_data, (void*)symbol->kernel_export->function_data.shim_data);
      }
    }
  }

  return 0;
}

int LLVMJIT::UninitModule(ExecModule* module) {
  return 0;
}

FunctionPointer LLVMJIT::GenerateFunction(FunctionSymbol* symbol) {
  // In theory this is only called when required, so just assume we need to
  // generate it.

  // Lock the function for adding.
  // If this fails it means the function exists and we don't need to generate
  // it.
  FunctionPointer ptr = fn_table_->BeginAddFunction(symbol->start_address);
  if (ptr) {
    return ptr;
  }

  printf("generating %s...\n", symbol->name());

  Function* fn = NULL;
  int result_code = cub_->MakeFunction(symbol, &fn);
  if (result_code) {
    XELOGE("Unable to generate function %s", symbol->name());
    return NULL;
  }

  // LLVM requires all functions called to be defined at the time of
  // getPointerToFunction, so this does a recursive depth-first walk
  // to generate them.
  for (std::vector<FunctionCall>::iterator it = symbol->outgoing_calls.begin();
     it != symbol->outgoing_calls.end(); ++it) {
    FunctionSymbol* target = it->target;
    printf("needs dep fn %s\n", target->name());
    GenerateFunction(target);
  }

  // Generate the machine code.
  ptr = (FunctionPointer)engine_->getPointerToFunction(fn);

  // Add to the function table.
  fn_table_->AddFunction(symbol->start_address, ptr);

  return ptr;
}
