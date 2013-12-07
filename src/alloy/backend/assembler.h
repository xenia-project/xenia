/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_ASSEMBLER_H_
#define ALLOY_BACKEND_ASSEMBLER_H_

#include <alloy/core.h>


namespace alloy {
namespace hir {
class FunctionBuilder;
}
namespace runtime {
class Function;
class FunctionInfo;
class Runtime;
}
}

namespace alloy {
namespace backend {

class Backend;


class Assembler {
public:
  Assembler(Backend* backend);
  virtual ~Assembler();

  virtual int Initialize();

  virtual void Reset();

  virtual int Assemble(
      runtime::FunctionInfo* symbol_info, hir::FunctionBuilder* builder,
      runtime::Function** out_function) = 0;

protected:
  Backend* backend_;
};


}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_ASSEMBLER_H_
