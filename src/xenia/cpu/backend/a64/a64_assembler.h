/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_ASSEMBLER_H_
#define XENIA_CPU_BACKEND_A64_A64_ASSEMBLER_H_

#include <memory>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Backend;
class A64Emitter;

class A64Assembler : public Assembler {
 public:
  explicit A64Assembler(A64Backend* backend);
  ~A64Assembler() override;

  bool Initialize() override;

  void Reset() override;

  bool Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                uint32_t debug_info_flags,
                std::unique_ptr<FunctionDebugInfo> debug_info) override;

 private:
  void DumpMachineCode(void* machine_code, size_t code_size,
                       const std::vector<SourceMapEntry>& source_map,
                       StringBuffer* str);

 private:
  A64Backend* a64_backend_;
  std::unique_ptr<A64Emitter> emitter_;

  StringBuffer string_buffer_;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_ASSEMBLER_H_
