/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_ASSEMBLER_H_
#define XENIA_BACKEND_ASSEMBLER_H_

#include <memory>

namespace xe {
namespace cpu {
class DebugInfo;
class Function;
class FunctionInfo;
namespace hir {
class HIRBuilder;
}  // namespace hir
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {

class Backend;

class Assembler {
 public:
  Assembler(Backend* backend);
  virtual ~Assembler();

  virtual bool Initialize();

  virtual void Reset();

  virtual bool Assemble(FunctionInfo* symbol_info, hir::HIRBuilder* builder,
                        uint32_t debug_info_flags,
                        std::unique_ptr<DebugInfo> debug_info,
                        Function** out_function) = 0;

 protected:
  Backend* backend_;
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_ASSEMBLER_H_
