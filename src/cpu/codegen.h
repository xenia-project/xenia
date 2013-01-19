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
#include <xenia/kernel/module.h>

#include <llvm/DIBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>


typedef struct {
  int reserved;
} xe_codegen_options_t;

typedef struct {
  xe_memory_ref       memory;
  xe_kernel_export_resolver_ref export_resolver;
  xe_module_ref       module;
  xe_sdb_ref          sdb;

  llvm::LLVMContext   *context;
  llvm::Module        *shared_module;
  llvm::Module        *gen_module;
  llvm::DIBuilder     *di_builder;
  llvm::MDNode        *cu;
} xe_codegen_ctx_t;


llvm::Module *xe_codegen(xe_codegen_ctx_t *ctx,
                         xe_codegen_options_t options);


#endif  // XENIA_CPU_CODEGEN_H_
