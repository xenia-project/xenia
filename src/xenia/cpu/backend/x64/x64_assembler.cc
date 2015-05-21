/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_assembler.h"

#include "xenia/base/reset_scope.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/cpu-private.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/processor.h"
#include "xenia/profiling.h"

namespace BE {
#include <beaengine/BeaEngine.h>
}  // namespace BE

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu;

using xe::cpu::hir::HIRBuilder;

X64Assembler::X64Assembler(X64Backend* backend)
    : Assembler(backend), x64_backend_(backend) {}

X64Assembler::~X64Assembler() {
  // Emitter must be freed before the allocator.
  emitter_.reset();
  allocator_.reset();
}

bool X64Assembler::Initialize() {
  if (!Assembler::Initialize()) {
    return false;
  }

  allocator_.reset(new XbyakAllocator());
  emitter_.reset(new X64Emitter(x64_backend_, allocator_.get()));

  return true;
}

void X64Assembler::Reset() {
  string_buffer_.Reset();
  Assembler::Reset();
}

bool X64Assembler::Assemble(FunctionInfo* symbol_info, HIRBuilder* builder,
                            uint32_t debug_info_flags,
                            std::unique_ptr<DebugInfo> debug_info,
                            Function** out_function) {
  SCOPE_profile_cpu_f("cpu");

  // Reset when we leave.
  xe::make_reset_scope(this);

  // Lower HIR -> x64.
  void* machine_code = nullptr;
  size_t code_size = 0;
  if (!emitter_->Emit(symbol_info->address(), builder, debug_info_flags,
                      debug_info.get(), machine_code, code_size)) {
    return false;
  }

  // Stash generated machine code.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmMachineCode) {
    DumpMachineCode(debug_info.get(), machine_code, code_size, &string_buffer_);
    debug_info->set_machine_code_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Dump debug data.
  //auto fn_data = backend_->processor()->debugger()->
  if (FLAGS_disassemble_functions) {
    if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmSource) {
      //
    }
  }

  {
    X64Function* fn = new X64Function(symbol_info);
    fn->set_debug_info(std::move(debug_info));
    fn->Setup(machine_code, code_size);

    *out_function = fn;
  }

  return true;
}

void X64Assembler::DumpMachineCode(DebugInfo* debug_info, void* machine_code,
                                   size_t code_size, StringBuffer* str) {
  BE::DISASM disasm = {0};
  disasm.Archi = 64;
  disasm.Options = BE::Tabulation + BE::MasmSyntax + BE::PrefixedNumeral;
  disasm.EIP = (BE::UIntPtr)machine_code;
  BE::UIntPtr eip_end = disasm.EIP + code_size;
  uint64_t prev_source_offset = 0;
  while (disasm.EIP < eip_end) {
    // Look up source offset.
    auto map_entry = debug_info->LookupCodeOffset(
        static_cast<uint32_t>(disasm.EIP - (BE::UIntPtr)machine_code));
    if (map_entry) {
      if (map_entry->source_offset == prev_source_offset) {
        str->Append("         ");
      } else {
        str->Append("%.8X ", map_entry->source_offset);
        prev_source_offset = map_entry->source_offset;
      }
    } else {
      str->Append("?        ");
    }

    size_t len = BE::Disasm(&disasm);
    if (len == BE::UNKNOWN_OPCODE) {
      break;
    }
    str->Append("%p  %s\n", disasm.EIP, disasm.CompleteInstr);
    disasm.EIP += len;
  }
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
