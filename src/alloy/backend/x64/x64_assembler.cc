/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_assembler.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_emitter.h>
#include <alloy/backend/x64/x64_function.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/hir/label.h>
#include <alloy/runtime/runtime.h>

namespace BE {
#include <beaengine/BeaEngine.h>
}

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::hir;
using namespace alloy::runtime;


X64Assembler::X64Assembler(X64Backend* backend) :
    x64_backend_(backend),
    emitter_(0), allocator_(0),
    Assembler(backend) {
}

X64Assembler::~X64Assembler() {
  alloy::tracing::WriteEvent(EventType::AssemblerDeinit({
  }));

  delete emitter_;
  delete allocator_;
}

int X64Assembler::Initialize() {
  int result = Assembler::Initialize();
  if (result) {
    return result;
  }

  allocator_ = new XbyakAllocator();
  emitter_ = new X64Emitter(x64_backend_, allocator_);

  alloy::tracing::WriteEvent(EventType::AssemblerInit({
  }));

  return result;
}

void X64Assembler::Reset() {
  string_buffer_.Reset();
  Assembler::Reset();
}

int X64Assembler::Assemble(
    FunctionInfo* symbol_info, HIRBuilder* builder,
    uint32_t debug_info_flags, DebugInfo* debug_info,
    Function** out_function) {
  int result = 0;

  // Lower HIR -> x64.
  void* machine_code = 0;
  size_t code_size = 0;
  result = emitter_->Emit(builder,
                          debug_info_flags, debug_info,
                          machine_code, code_size);
  XEEXPECTZERO(result);

  // Stash generated machine code.
  if (debug_info_flags & DEBUG_INFO_MACHINE_CODE_DISASM) {
    DumpMachineCode(debug_info, machine_code, code_size, &string_buffer_);
    debug_info->set_machine_code_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  X64Function* fn = new X64Function(symbol_info);
  fn->set_debug_info(debug_info);
  fn->Setup(machine_code, code_size);

  *out_function = fn;

  result = 0;

XECLEANUP:
  Reset();
  return result;
}

void X64Assembler::DumpMachineCode(
    DebugInfo* debug_info,
    void* machine_code, size_t code_size,
    StringBuffer* str) {
  BE::DISASM disasm;
  xe_zero_struct(&disasm, sizeof(disasm));
  disasm.Archi = 64;
  disasm.Options = BE::Tabulation + BE::MasmSyntax + BE::PrefixedNumeral;
  disasm.EIP = (BE::UIntPtr)machine_code;
  BE::UIntPtr eip_end = disasm.EIP + code_size;
  uint64_t prev_source_offset = 0;
  while (disasm.EIP < eip_end) {
    // Look up source offset.
    auto map_entry = debug_info->LookupCodeOffset(
        disasm.EIP - (BE::UIntPtr)machine_code);
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
