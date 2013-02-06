/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/llvm_exports.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <xenia/cpu/sdb.h>
#include <xenia/cpu/ppc/instr.h>
#include <xenia/cpu/ppc/state.h>
#include <xenia/kernel/export.h>


using namespace llvm;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


namespace {


void XeTrap(xe_ppc_state_t* state, uint32_t cia) {
  XELOGE(XT("TRAP"));
  XEASSERTALWAYS();
}

void XeIndirectBranch(xe_ppc_state_t* state, uint64_t target, uint64_t br_ia) {
  XELOGCPU(XT("INDIRECT BRANCH %.8X -> %.8X"),
           (uint32_t)br_ia, (uint32_t)target);
  XEASSERTALWAYS();
}

void XeInvalidInstruction(xe_ppc_state_t* state, uint32_t cia, uint32_t data) {
  ppc::InstrData i;
  i.address = cia;
  i.code = data;
  i.type = ppc::GetInstrType(i.code);

  if (!i.type) {
    XELOGCPU(XT("INVALID INSTRUCTION %.8X: %.8X ???"),
             i.address, i.code);
  } else if (i.type->disassemble) {
    ppc::InstrDisasm d;
    i.type->disassemble(i, d);
    std::string disasm;
    d.Dump(disasm);
    XELOGCPU(XT("INVALID INSTRUCTION %.8X: %.8X %s"),
             i.address, i.code, disasm.c_str());
  } else {
    XELOGCPU(XT("INVALID INSTRUCTION %.8X: %.8X %s"),
             i.address, i.code, i.type->name);
  }
}

void XeAccessViolation(xe_ppc_state_t* state, uint32_t cia, uint64_t ea) {
  XELOGE(XT("INVALID ACCESS %.8X: tried to touch %.8X"),
         cia, (uint32_t)ea);
  XEASSERTALWAYS();
}

void XeTraceKernelCall(xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
                       KernelExport* kernel_export) {
  XELOGCPU(XT("TRACE: %.8X -> k.%.8X (%s)"),
           (uint32_t)call_ia - 4, (uint32_t)cia,
           kernel_export ? kernel_export->name : "unknown");
}

void XeTraceUserCall(xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
                     FunctionSymbol* fn) {
  XELOGCPU(XT("TRACE: %.8X -> u.%.8X (%s)"),
           (uint32_t)call_ia - 4, (uint32_t)cia, fn->name());
}

void XeTraceInstruction(xe_ppc_state_t* state, uint32_t cia, uint32_t data) {
  ppc::InstrType* type = ppc::GetInstrType(data);
  XELOGCPU(XT("TRACE: %.8X %.8X %s %s"),
           cia, data,
           type && type->emit ? " " : "X",
           type ? type->name : "<unknown>");

  if (cia == 0x82014468) {
    printf("BREAKBREAKBREAK\n");
  }

  // TODO(benvanik): better disassembly, printing of current register values/etc
}


}


void xe::cpu::SetupLlvmExports(llvm::Module* module,
                               const llvm::DataLayout* dl,
                               llvm::ExecutionEngine* engine) {
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
      module), (void*)&XeTrap);

  std::vector<Type*> indirectBranchArgs;
  indirectBranchArgs.push_back(int8PtrTy);
  indirectBranchArgs.push_back(Type::getInt64Ty(context));
  indirectBranchArgs.push_back(Type::getInt64Ty(context));
  FunctionType* indirectBranchTy = FunctionType::get(
      Type::getVoidTy(context), indirectBranchArgs, false);
  engine->addGlobalMapping(Function::Create(
      indirectBranchTy, Function::ExternalLinkage, "XeIndirectBranch",
      module), (void*)&XeIndirectBranch);

  // Debugging methods:
  std::vector<Type*> invalidInstructionArgs;
  invalidInstructionArgs.push_back(int8PtrTy);
  invalidInstructionArgs.push_back(Type::getInt32Ty(context));
  invalidInstructionArgs.push_back(Type::getInt32Ty(context));
  FunctionType* invalidInstructionTy = FunctionType::get(
      Type::getVoidTy(context), invalidInstructionArgs, false);
  engine->addGlobalMapping(Function::Create(
      invalidInstructionTy, Function::ExternalLinkage, "XeInvalidInstruction",
      module), (void*)&XeInvalidInstruction);

  std::vector<Type*> accessViolationArgs;
  accessViolationArgs.push_back(int8PtrTy);
  accessViolationArgs.push_back(Type::getInt32Ty(context));
  accessViolationArgs.push_back(Type::getInt64Ty(context));
  FunctionType* accessViolationTy = FunctionType::get(
      Type::getVoidTy(context), accessViolationArgs, false);
  engine->addGlobalMapping(Function::Create(
      accessViolationTy, Function::ExternalLinkage, "XeAccessViolation",
      module), (void*)&XeAccessViolation);

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
      module), (void*)&XeTraceKernelCall);
  engine->addGlobalMapping(Function::Create(
      traceCallTy, Function::ExternalLinkage, "XeTraceUserCall",
      module), (void*)&XeTraceUserCall);
  engine->addGlobalMapping(Function::Create(
      traceInstructionTy, Function::ExternalLinkage, "XeTraceInstruction",
      module), (void*)&XeTraceInstruction);
}
