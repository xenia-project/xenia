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
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/ppc.h>

using namespace llvm;


void xe_cpu_codegen_add_imports(xe_memory_ref memory,
                                xe_kernel_export_resolver_ref export_resolver,
                                xe_module_ref module, Module *m);
void xe_cpu_codegen_add_missing_import(
    Module *m, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export);
void xe_cpu_codegen_add_import(
    Module *m, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export);
void xe_cpu_codegen_optimize(Module *m, Function *fn);


llvm::Module *xe_cpu_codegen(llvm::LLVMContext& context, xe_memory_ref memory,
                             xe_kernel_export_resolver_ref export_resolver,
                             xe_module_ref module, Module *shared_module,
                             xe_codegen_options_t options) {
  std::string error_message;

  // Initialize the module.
  Module *m = new Module("generated.xex", context);
  // TODO(benavnik): addModuleFlag?

  // Link shared module into generated module.
  // This gives us a single module that we can optimize and prevents the need
  // for foreward declarations.
  Linker::LinkModules(m, shared_module, 0, &error_message);

  // Add import thunks/etc.
  xe_cpu_codegen_add_imports(memory, export_resolver, module, m);

  // Add export wrappers.
  //

  xe_xex2_ref xex = xe_module_get_xex(module);
  const xe_xex2_header_t *header = xe_xex2_get_header(xex);
  uint8_t *mem = xe_memory_addr(memory, 0);
  uint32_t *pc = (uint32_t*)(mem + header->exe_entry_point);
  uint32_t pcdata = XEGETUINT32BE(pc);
  printf("data %.8X %.8X\n", header->exe_entry_point, pcdata);
  xe_ppc_instr_type_t *instr_type = xe_ppc_get_instr_type(pcdata);
  if (instr_type) {
    printf("instr %.8X %s\n", header->exe_entry_point, instr_type->name);
    xe_ppc_instr_t instr;
    instr.data.code = pcdata;
    printf("%d %d\n", instr.data.XFX.D, instr.data.XFX.spr);
  } else {
    printf("instr not found\n");
  }

  Constant* c = m->getOrInsertFunction("mul_add",
  /*ret type*/                         IntegerType::get(context, 32),
  /*args*/                             IntegerType::get(context, 32),
                                       IntegerType::get(context, 32),
                                       IntegerType::get(context, 32),
  /*varargs terminated with null*/     NULL);

  Function* mul_add = cast<Function>(c);
  mul_add->setCallingConv(CallingConv::C);

  Function::arg_iterator args = mul_add->arg_begin();
  Value* x = args++;
  x->setName("x");
  Value* y = args++;
  y->setName("y");
  Value* z = args++;
  z->setName("z");

  BasicBlock* block = BasicBlock::Create(getGlobalContext(), "entry", mul_add);
  IRBuilder<> builder(block);

  Value* tmp = builder.CreateBinOp(Instruction::Mul,
                                   x, y, "tmp");
  Value* tmp2 = builder.CreateBinOp(Instruction::Add,
                                    tmp, z, "tmp2");

  builder.CreateRet(tmp2);

  // Run the optimizer on the function.
  // Doing this here keeps the size of the IR small and speeds up the later
  // passes.
  xe_cpu_codegen_optimize(m, mul_add);

  return m;
}

void xe_cpu_codegen_add_imports(xe_memory_ref memory,
                                xe_kernel_export_resolver_ref export_resolver,
                                xe_module_ref module, Module *m) {
  xe_xex2_ref xex = xe_module_get_xex(module);
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
              export_resolver, library->name, info->ordinal);
      if (!kernel_export || !xe_kernel_export_is_implemented(kernel_export)) {
        // Not implemented or known.
        xe_cpu_codegen_add_missing_import(m, library, info, kernel_export);
      } else {
        // Implemented.
        xe_cpu_codegen_add_import(m, library, info, kernel_export);
      }
    }

    xe_free(import_infos);
  }

  xe_xex2_release(xex);
}

void xe_cpu_codegen_add_missing_import(
    Module *m, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export) {
  LLVMContext& context = m->getContext();

  char name[128];
  xesnprintfa(name, XECOUNT(name), "__%s_%.8X",
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

    BasicBlock* block = BasicBlock::Create(context, "entry", f);
    IRBuilder<> builder(block);
    Value *tmp = builder.getInt32(0);
    builder.getInt32(123);
    builder.CreateRet(tmp);

    xe_cpu_codegen_optimize(m, f);

    //GlobalAlias *alias = new GlobalAlias(f->getType(), GlobalValue::InternalLinkage, name, f, m);
  //   printf("   F %.8X %.8X %.3X (%3d) %s %s\n",
  //          info->value_address, info->thunk_address, info->ordinal,
  //          info->ordinal, implemented ? "  " : "!!", name);
  } else {
  //   printf("   V %.8X          %.3X (%3d) %s %s\n",
  //          info->value_address, info->ordinal, info->ordinal,
  //          implemented ? "  " : "!!", name);
  }
}

void xe_cpu_codegen_add_import(
    Module *m, const xe_xex2_import_library_t *library,
    const xe_xex2_import_info_t* info, xe_kernel_export_t *kernel_export) {
  //
}

void xe_cpu_codegen_optimize(Module *m, Function *fn) {
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
