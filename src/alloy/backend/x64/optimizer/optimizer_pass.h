/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_PASS_H_
#define ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_PASS_H_

#include <alloy/core.h>

#include <alloy/backend/x64/lir/lir_builder.h>

namespace alloy { namespace runtime { class Runtime; } }


namespace alloy {
namespace backend {
namespace x64 {
namespace optimizer {

class Optimizer;


class OptimizerPass {
public:
  OptimizerPass();
  virtual ~OptimizerPass();

  virtual int Initialize(Optimizer* optimizer);

  virtual int Run(lir::LIRBuilder* builder) = 0;

protected:
  runtime::Runtime* runtime_;
  Optimizer* optimizer_;
};


}  // namespace optimizer
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_OPTIMIZER_OPTIMIZER_PASS_H_
