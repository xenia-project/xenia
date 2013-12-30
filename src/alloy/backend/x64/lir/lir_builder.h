/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_
#define ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_

#include <alloy/core.h>
#include <alloy/backend/x64/lir/lir_opcodes.h>


namespace alloy {
namespace backend {
namespace x64 {
class X64Backend;
namespace lir {


class LIRBuilder {
public:
  LIRBuilder(X64Backend* backend);
  ~LIRBuilder();

  void Reset();
  int Finalize();

  void Dump(StringBuffer* str);

  Arena* arena() const { return arena_; }

protected:
  X64Backend* backend_;
  Arena*      arena_;
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_BUILDER_H_
