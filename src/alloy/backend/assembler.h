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

#include <memory>

#include <alloy/core.h>

namespace alloy {
namespace hir {
class HIRBuilder;
}  // namespace hir
namespace runtime {
class DebugInfo;
class Function;
class FunctionInfo;
class Runtime;
}  // namespace runtime
}  // namespace alloy

namespace alloy {
namespace backend {

class Backend;

class Assembler {
 public:
  Assembler(Backend* backend);
  virtual ~Assembler();

  virtual int Initialize();

  virtual void Reset();

  virtual int Assemble(runtime::FunctionInfo* symbol_info,
                       hir::HIRBuilder* builder, uint32_t debug_info_flags,
                       std::unique_ptr<runtime::DebugInfo> debug_info,
                       runtime::Function** out_function) = 0;

 protected:
  Backend* backend_;
};

}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_ASSEMBLER_H_
