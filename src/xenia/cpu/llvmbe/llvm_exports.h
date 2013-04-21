/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVMBE_LLVM_EXPORTS_H_
#define XENIA_CPU_LLVMBE_LLVM_EXPORTS_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/global_exports.h>


namespace llvm {
  class ExecutionEngine;
  class Module;
  class DataLayout;
}


namespace xe {
namespace cpu {
namespace llvmbe {


void SetupLlvmExports(
    GlobalExports* global_exports,
    llvm::Module* module, const llvm::DataLayout* dl,
    llvm::ExecutionEngine* engine);


}  // llvmbe
}  // cpu
}  // xe


#endif  // XENIA_CPU_LLVMBE_LLVM_EXPORTS_H_
