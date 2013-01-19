/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cpu/codegen.h"

#include <llvm/Linker.h>
#include <llvm/PassManager.h>
#include <llvm/DebugInfo.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/ppc.h>

using namespace llvm;


void xe_codegen_add_imports(xe_codegen_ctx_t *ctx);
void xe_codegen_add_missing_import(
    xe_codegen_ctx_t *ctx, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export);
void xe_codegen_add_import(
    xe_codegen_ctx_t *ctx, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export);
void xe_codegen_add_function(xe_codegen_ctx_t *ctx, xe_sdb_function_t *fn);
void xe_codegen_optimize(Module *m, Function *fn);


llvm::Module *xe_codegen(xe_codegen_ctx_t *ctx,
                         xe_codegen_options_t options) {
  LLVMContext& context = *ctx->context;
  std::string error_message;

  // Initialize the module.
  Module *m = new Module(xe_module_get_name(ctx->module), context);
  ctx->gen_module = m;
  // TODO(benavnik): addModuleFlag?

  // Link shared module into generated module.
  // This gives us a single module that we can optimize and prevents the need
  // for foreward declarations.
  Linker::LinkModules(m, ctx->shared_module, 0, &error_message);

  // Setup a debug info builder.
  // This is used when creating any debug info. We may want to go more
  // fine grained than this, but for now it's something.
  xechar_t dir[2048];
  xestrcpy(dir, XECOUNT(dir), xe_module_get_path(ctx->module));
  xechar_t *slash = xestrrchr(dir, '/');
  if (slash) {
    *(slash + 1) = 0;
  }
  ctx->di_builder = new DIBuilder(*m);
  ctx->di_builder->createCompileUnit(
      0,
      StringRef(xe_module_get_name(ctx->module)),
      StringRef(dir),
      StringRef("xenia"),
      true,
      StringRef(""),
      0);
  ctx->cu = (MDNode*)ctx->di_builder->getCU();

  // Add import thunks/etc.
  xe_codegen_add_imports(ctx);

  // Add export wrappers.
  //

  // Add all functions.
  xe_sdb_function_t **functions;
  size_t function_count;
  if (!xe_sdb_get_functions(ctx->sdb, &functions, &function_count)) {
    for (size_t n = 0; n < function_count; n++) {
      // kernel functions will be handled by the add imports handlers.
      if (functions[n]->type == kXESDBFunctionUser) {
        xe_codegen_add_function(ctx, functions[n]);
      }
    }
    xe_free(functions);
  }

  ctx->di_builder->finalize();

  return m;
}

void xe_codegen_add_imports(xe_codegen_ctx_t *ctx) {
  xe_xex2_ref xex = xe_module_get_xex(ctx->module);
  const xe_xex2_header_t *header = xe_xex2_get_header(xex);

  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t *library = &header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    XEIGNORE(xe_xex2_get_import_infos(xex, library,
                                      &import_infos, &import_info_count));

    for (size_t i = 0; i < import_info_count; i++) {
      const xe_xex2_import_info_t *info = &import_infos[i];
      xe_kernel_export_t *kernel_export =
          xe_kernel_export_resolver_get_by_ordinal(
              ctx->export_resolver, library->name, info->ordinal);
      if (!kernel_export || !xe_kernel_export_is_implemented(kernel_export)) {
        // Not implemented or known.
        xe_codegen_add_missing_import(ctx, library, info, kernel_export);
      } else {
        // Implemented.
        xe_codegen_add_import(ctx, library, info, kernel_export);
      }
    }

    xe_free(import_infos);
  }

  xe_xex2_release(xex);
}

void xe_codegen_add_missing_import(
    xe_codegen_ctx_t *ctx, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export) {
  Module *m = ctx->gen_module;
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
    Type *return_type = Type::getInt32Ty(context);

    FunctionType *ft = FunctionType::get(return_type,
                                         ArrayRef<Type*>(args), false);
    Function *f = cast<Function>(m->getOrInsertFunction(
        StringRef(name), ft, attrs));
    f->setCallingConv(CallingConv::C);
    f->setVisibility(GlobalValue::DefaultVisibility);

    // TODO(benvanik): log errors.
    BasicBlock* block = BasicBlock::Create(context, "entry", f);
    IRBuilder<> builder(block);
    Value *tmp = builder.getInt32(0);
    builder.CreateRet(tmp);

    xe_codegen_optimize(m, f);

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

void xe_codegen_add_import(
    xe_codegen_ctx_t *ctx, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export) {
  // Module *m = ctx->gen_module;
  // LLVMContext& context = m->getContext();

  // TODO(benvanik): add import thunk code.
}

void xe_codegen_add_function(xe_codegen_ctx_t *ctx, xe_sdb_function_t *fn) {
  Module *m = ctx->gen_module;
  LLVMContext& context = m->getContext();

  AttributeWithIndex awi[] = {
    //AttributeWithIndex::get(context, 2, Attributes::NoCapture),
    AttributeWithIndex::get(context,
        AttributeSet::FunctionIndex, Attribute::NoUnwind),
  };
  AttributeSet attrs = AttributeSet::get(context, awi);

  std::vector<Type*> args;
  Type *return_type = Type::getInt32Ty(context);

  char name[64];
  char *pname = name;
  if (fn->name) {
    pname = fn->name;
  } else {
    xesnprintfa(name, XECOUNT(name), "fn_%.8X", fn->start_address);
  }

  FunctionType *ft = FunctionType::get(return_type,
                                       ArrayRef<Type*>(args), false);
  Function *f = cast<Function>(m->getOrInsertFunction(
      StringRef(pname), ft, attrs));
  f->setCallingConv(CallingConv::C);
  f->setVisibility(GlobalValue::DefaultVisibility);

  // TODO(benvanik): generate code!
  BasicBlock* block = BasicBlock::Create(context, "entry", f);
  IRBuilder<> builder(block);
  //builder.SetCurrentDebugLocation(DebugLoc::get(fn->start_address >> 8, fn->start_address & 0xFF, ctx->cu));
  Value *tmp = builder.getInt32(0);
  builder.CreateRet(tmp);

  // i->setMetadata("some.name", MDNode::get(context, MDString::get(context, pname)));

  uint8_t *mem = xe_memory_addr(ctx->memory, 0);
  uint32_t *pc = (uint32_t*)(mem + fn->start_address);
  uint32_t pcdata = XEGETUINT32BE(pc);
  printf("data %.8X %.8X\n", fn->start_address, pcdata);
  xe_ppc_instr_type_t *instr_type = xe_ppc_get_instr_type(pcdata);
  if (instr_type) {
    printf("instr %.8X %s\n", fn->start_address, instr_type->name);
    xe_ppc_instr_t instr;
    instr.data.code = pcdata;
    printf("%d %d\n", instr.data.XFX.D, instr.data.XFX.spr);
  } else {
    printf("instr not found\n");
  }

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  xe_codegen_optimize(m, f);
}

void xe_codegen_optimize(Module *m, Function *fn) {
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
