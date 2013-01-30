/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVM_EXPORTS_H_
#define XENIA_CPU_LLVM_EXPORTS_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace llvm {
  class ExecutionEngine;
  class LLVMContext;
  class Module;
  class DataLayout;
}


namespace xe {
namespace cpu {


void SetupLlvmExports(llvm::Module* module,
                      const llvm::DataLayout* dl,
                      llvm::ExecutionEngine* engine);


}  // cpu
}  // xe


#endif  // XENIA_CPU_LLVM_EXPORTS_H_
