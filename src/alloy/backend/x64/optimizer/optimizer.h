/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_H_
#define ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_H_

#include <alloy/core.h>
#include <alloy/backend/x64/lir/lir_builder.h>

namespace alloy { namespace runtime { class Runtime; } }


namespace alloy {
namespace backend {
namespace x64 {
namespace optimizer {

class OptimizerPass;


class Optimizer {
public:
  Optimizer(runtime::Runtime* runtime);
  ~Optimizer();

  runtime::Runtime* runtime() const { return runtime_; }

  void AddPass(OptimizerPass* pass);

  void Reset();

  int Optimize(lir::LIRBuilder* builder);

private:
  runtime::Runtime* runtime_;

  typedef std::vector<OptimizerPass*> PassList;
  PassList passes_;
};


}  // namespace optimizer
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_H_
