/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_ASSEMBLER_H_
#define ALLOY_BACKEND_X64_X64_ASSEMBLER_H_

#include <memory>

#include <alloy/core.h>

#include <alloy/string_buffer.h>
#include <alloy/backend/assembler.h>

namespace alloy {
namespace backend {
namespace x64 {

class X64Backend;
class X64Emitter;
class XbyakAllocator;

class X64Assembler : public Assembler {
 public:
  X64Assembler(X64Backend* backend);
  ~X64Assembler() override;

  int Initialize() override;

  void Reset() override;

  int Assemble(runtime::FunctionInfo* symbol_info, hir::HIRBuilder* builder,
               uint32_t debug_info_flags,
               std::unique_ptr<runtime::DebugInfo> debug_info,
               runtime::Function** out_function) override;

 private:
  void DumpMachineCode(runtime::DebugInfo* debug_info, void* machine_code,
                       size_t code_size, StringBuffer* str);

 private:
  X64Backend* x64_backend_;
  std::unique_ptr<X64Emitter> emitter_;
  std::unique_ptr<XbyakAllocator> allocator_;

  StringBuffer string_buffer_;
};

}  // namespace x64
}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_X64_X64_ASSEMBLER_H_
