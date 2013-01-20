/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CODEGEN_H_
#define XENIA_CPU_CODEGEN_H_

#include <xenia/cpu/sdb.h>
#include <xenia/core/memory.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/user_module.h>


namespace llvm {
  class LLVMContext;
  class Module;
  class Function;
  class DIBuilder;
  class MDNode;
}


namespace xe {
namespace cpu {
namespace codegen {


class CodegenContext {
public:
  CodegenContext(
      xe_memory_ref memory, kernel::ExportResolver* export_resolver,
      kernel::UserModule* module, sdb::SymbolDatabase* sdb,
      llvm::LLVMContext* context, llvm::Module* gen_module);
  ~CodegenContext();

  int GenerateModule();

private:
  void AddImports();
  void AddMissingImport(
      const xe_xex2_import_library_t *library,
      const xe_xex2_import_info_t* info, kernel::KernelExport* kernel_export);
  void AddPresentImport(
      const xe_xex2_import_library_t *library,
      const xe_xex2_import_info_t* info, kernel::KernelExport* kernel_export);
  void AddFunction(sdb::FunctionSymbol* fn);
  void OptimizeFunction(llvm::Module* m, llvm::Function* fn);

  xe_memory_ref memory_;
  kernel::ExportResolver* export_resolver_;
  kernel::UserModule* module_;
  sdb::SymbolDatabase* sdb_;

  llvm::LLVMContext*  context_;
  llvm::Module*       gen_module_;
  llvm::DIBuilder*    di_builder_;
  llvm::MDNode*       cu_;
};


}  // namespace codegen
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODEGEN_H_
