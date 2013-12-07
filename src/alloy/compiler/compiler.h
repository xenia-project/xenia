/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_COMPILER_H_
#define ALLOY_COMPILER_COMPILER_H_

#include <alloy/core.h>
#include <alloy/hir/function_builder.h>

namespace alloy { namespace runtime { class Runtime; } }


namespace alloy {
namespace compiler {

class Pass;


class Compiler {
public:
  Compiler(runtime::Runtime* runtime);
  ~Compiler();

  runtime::Runtime* runtime() const { return runtime_; }
  
  void AddPass(Pass* pass);

  void Reset();

  int Compile(hir::FunctionBuilder* builder);

private:
  runtime::Runtime* runtime_;

  typedef std::vector<Pass*> PassList;
  PassList passes_;
};


}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_COMPILER_H_
