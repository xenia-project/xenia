/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_ASSEMBLER_H_
#define XENIA_CPU_BACKEND_X64_X64_ASSEMBLER_H_

#include <memory>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64Backend;
class X64Emitter;
class XbyakAllocator;

class X64Assembler : public Assembler {
 public:
  explicit X64Assembler(X64Backend* backend);
  ~X64Assembler() override;

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
  X64Backend* x64_backend_;
  std::unique_ptr<X64Emitter> emitter_;
  std::unique_ptr<XbyakAllocator> allocator_;
  uintptr_t capstone_handle_;

  StringBuffer string_buffer_;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_ASSEMBLER_H_
