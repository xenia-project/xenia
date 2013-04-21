/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVMBE_LLVM_BACKEND_H_
#define XENIA_CPU_LLVMBE_LLVM_BACKEND_H_

#include <xenia/common.h>

#include <xenia/cpu/backend.h>


namespace xe {
namespace cpu {
namespace llvmbe {


class LLVMBackend : public Backend {
public:
  LLVMBackend();
  virtual ~LLVMBackend();

  virtual CodeUnitBuilder* CreateCodeUnitBuilder();
  virtual LibraryLinker* CreateLibraryLinker();
  virtual LibraryLoader* CreateLibraryLoader();

  virtual JIT* CreateJIT(xe_memory_ref memory, FunctionTable* fn_table);

protected:
};


}  // namespace llvmbe
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LLVMBE_LLVM_BACKEND_H_
