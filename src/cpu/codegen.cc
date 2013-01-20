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

  // Add import thunks/etc.
  AddImports();

  // Add export wrappers.
  //

  // Add all functions.
  std::vector<FunctionSymbol*> functions;
  if (!sdb_->GetAllFunctions(functions)) {
    for (std::vector<FunctionSymbol*>::iterator it = functions.begin();
         it != functions.end(); ++it) {
      // kernel functions will be handled by the add imports handlers.
      if ((*it)->type == FunctionSymbol::User) {
        AddFunction(*it);
      }
    }
  }

  di_builder_->finalize();

  return 0;
}

void CodegenContext::AddImports() {
  xe_xex2_ref xex = module_->xex();
  const xe_xex2_header_t* header = xe_xex2_get_header(xex);

  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t* library = &header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    XEIGNORE(xe_xex2_get_import_infos(xex, library,
                                      &import_infos, &import_info_count));

    for (size_t i = 0; i < import_info_count; i++) {
      const xe_xex2_import_info_t* info = &import_infos[i];
      KernelExport* kernel_export = export_resolver_->GetExportByOrdinal(
          library->name, info->ordinal);
      if (!kernel_export || !kernel_export->IsImplemented()) {
        // Not implemented or known.
        AddMissingImport(library, info, kernel_export);
      } else {
        // Implemented.
        AddPresentImport(library, info, kernel_export);
      }
    }

    xe_free(import_infos);
  }

  xe_xex2_release(xex);
}

void CodegenContext::AddMissingImport(
    const xe_xex2_import_library_t* library,
    const xe_xex2_import_info_t* info, KernelExport* kernel_export) {
  Module* m = gen_module_;
  LLVMContext& context = m->getContext();

  char name[128];
  xesnprintfa(name, XECOUNT(name), "__thunk_%s_%.8X",
              library->name, kernel_export->ordinal);

  // TODO(benvanik): add name as comment/alias?
  // name = kernel_export->name;

  if (info->thunk_address) {
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
        StringRef(name), ft, attrs));
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
  } else {
    // printf("   V %.8X          %.3X (%3d) %s %s\n",
    //        info->value_address, info->ordinal, info->ordinal,
    //        implemented ? "  " : "!!", name);
  }
}

void CodegenContext::AddPresentImport(
    const xe_xex2_import_library_t* library,
    const xe_xex2_import_info_t* info, KernelExport* kernel_export) {
  // Module *m = gen_module_;
  // LLVMContext& context = m->getContext();

  // TODO(benvanik): add import thunk code.
}

void CodegenContext::AddFunction(FunctionSymbol* fn) {
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
    xesnprintfa(name, XECOUNT(name), "fn_%.8X", fn->start_address);
  }

  FunctionType* ft = FunctionType::get(return_type,
                                       ArrayRef<Type*>(args), false);
  Function* f = cast<Function>(
      m->getOrInsertFunction(StringRef(pname), ft, attrs));
  f->setCallingConv(CallingConv::C);
  f->setVisibility(GlobalValue::DefaultVisibility);

  // TODO(benvanik): generate code!
  BasicBlock* block = BasicBlock::Create(context, "entry", f);
  IRBuilder<> builder(block);
  //builder.SetCurrentDebugLocation(DebugLoc::get(fn->start_address >> 8, fn->start_address & 0xFF, ctx->cu));
  Value* tmp = builder.getInt32(0);
  builder.CreateRet(tmp);

  // i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));

  uint8_t* mem = xe_memory_addr(memory_, 0);
  uint32_t* pc = (uint32_t*)(mem + fn->start_address);
  uint32_t pcdata = XEGETUINT32BE(pc);
  printf("data %.8X %.8X\n", fn->start_address, pcdata);
  InstrType* instr_type = ppc::GetInstrType(pcdata);
  if (instr_type) {
    printf("instr %.8X %s\n", fn->start_address, instr_type->name);
    // xe_ppc_instr_t instr;
    // instr.data.code = pcdata;
    // printf("%d %d\n", instr.data.XFX.D, instr.data.XFX.spr);
  } else {
    printf("instr not found\n");
  }

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  OptimizeFunction(m, f);
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
