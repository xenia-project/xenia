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

#include <alloy/core.h>

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
  virtual ~X64Assembler();

  virtual int Initialize();

  virtual void Reset();

  virtual int Assemble(
      runtime::FunctionInfo* symbol_info, hir::HIRBuilder* builder,
      uint32_t debug_info_flags, runtime::DebugInfo* debug_info,
      runtime::Function** out_function);

private:
  void DumpMachineCode(runtime::DebugInfo* debug_info,
                       void* machine_code, size_t code_size,
                       StringBuffer* str);

private:
  X64Backend*           x64_backend_;
  X64Emitter*           emitter_;
  XbyakAllocator*       allocator_;

  StringBuffer          string_buffer_;
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_ASSEMBLER_H_
