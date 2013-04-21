/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvmbe/llvm_exports.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;


void xe::cpu::llvmbe::SetupLlvmExports(
    GlobalExports* global_exports,
    Module* module, const DataLayout* dl, ExecutionEngine* engine) {
  LLVMContext& context = module->getContext();
  Type* int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(context));

  // Control methods:
  std::vector<Type*> trapArgs;
  trapArgs.push_back(int8PtrTy);
  trapArgs.push_back(Type::getInt32Ty(context));
  FunctionType* trapTy = FunctionType::get(
      Type::getVoidTy(context), trapArgs, false);
  engine->addGlobalMapping(Function::Create(
      trapTy, Function::ExternalLinkage, "XeTrap",
      module), (void*)(global_exports->XeTrap));

  std::vector<Type*> indirectBranchArgs;
  indirectBranchArgs.push_back(int8PtrTy);
  indirectBranchArgs.push_back(Type::getInt64Ty(context));
  indirectBranchArgs.push_back(Type::getInt64Ty(context));
  FunctionType* indirectBranchTy = FunctionType::get(
      Type::getVoidTy(context), indirectBranchArgs, false);
  engine->addGlobalMapping(Function::Create(
      indirectBranchTy, Function::ExternalLinkage, "XeIndirectBranch",
      module), (void*)(global_exports->XeIndirectBranch));

  // Debugging methods:
  std::vector<Type*> invalidInstructionArgs;
  invalidInstructionArgs.push_back(int8PtrTy);
  invalidInstructionArgs.push_back(Type::getInt32Ty(context));
  invalidInstructionArgs.push_back(Type::getInt32Ty(context));
  FunctionType* invalidInstructionTy = FunctionType::get(
      Type::getVoidTy(context), invalidInstructionArgs, false);
  engine->addGlobalMapping(Function::Create(
      invalidInstructionTy, Function::ExternalLinkage, "XeInvalidInstruction",
      module), (void*)(global_exports->XeInvalidInstruction));

  std::vector<Type*> accessViolationArgs;
  accessViolationArgs.push_back(int8PtrTy);
  accessViolationArgs.push_back(Type::getInt32Ty(context));
  accessViolationArgs.push_back(Type::getInt64Ty(context));
  FunctionType* accessViolationTy = FunctionType::get(
      Type::getVoidTy(context), accessViolationArgs, false);
  engine->addGlobalMapping(Function::Create(
      accessViolationTy, Function::ExternalLinkage, "XeAccessViolation",
      module), (void*)(global_exports->XeAccessViolation));

  // Tracing methods:
  std::vector<Type*> traceCallArgs;
  traceCallArgs.push_back(int8PtrTy);
  traceCallArgs.push_back(Type::getInt64Ty(context));
  traceCallArgs.push_back(Type::getInt64Ty(context));
  traceCallArgs.push_back(Type::getInt64Ty(context));
  FunctionType* traceCallTy = FunctionType::get(
      Type::getVoidTy(context), traceCallArgs, false);
  std::vector<Type*> traceInstructionArgs;
  traceInstructionArgs.push_back(int8PtrTy);
  traceInstructionArgs.push_back(Type::getInt32Ty(context));
  traceInstructionArgs.push_back(Type::getInt32Ty(context));
  FunctionType* traceInstructionTy = FunctionType::get(
      Type::getVoidTy(context), traceInstructionArgs, false);

  engine->addGlobalMapping(Function::Create(
      traceCallTy, Function::ExternalLinkage, "XeTraceKernelCall",
      module), (void*)(global_exports->XeTraceKernelCall));
  engine->addGlobalMapping(Function::Create(
      traceCallTy, Function::ExternalLinkage, "XeTraceUserCall",
      module), (void*)(global_exports->XeTraceUserCall));
  engine->addGlobalMapping(Function::Create(
      traceInstructionTy, Function::ExternalLinkage, "XeTraceInstruction",
      module), (void*)(global_exports->XeTraceInstruction));
}
