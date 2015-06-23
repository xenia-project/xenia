/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "Xenia.Debug.Native/disassembler.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/frontend/ppc_disasm.h"
#include "third_party/capstone/include/capstone.h"
#include "third_party/capstone/include/x86.h"

namespace Xenia {
namespace Debug {
namespace Native {

using namespace System;
using namespace System::Text;

Disassembler::Disassembler()
    : capstone_handle_(0), string_buffer_(new xe::StringBuffer()) {
  uintptr_t capstone_handle;
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle) != CS_ERR_OK) {
    System::Diagnostics::Debug::Fail("Failed to initialize capstone");
    return;
  }
  capstone_handle_ = capstone_handle;
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
}

Disassembler::~Disassembler() {
  if (capstone_handle_) {
    pin_ptr<uintptr_t> capstone_handle = &capstone_handle_;
    cs_close(capstone_handle);
  }
  delete string_buffer_;
}

String ^ Disassembler::DisassemblePPC(IntPtr code_address, size_t code_size) {
  string_buffer_->Reset();

  auto code_base = reinterpret_cast<const uint32_t*>(code_address.ToPointer());
  for (int i = 0; i < code_size / 4; ++i) {
    xe::cpu::frontend::InstrData instr;
    instr.address = uint32_t(code_address.ToInt64()) + i * 4;
    instr.code = xe::byte_swap(code_base[i]);
    instr.type = xe::cpu::frontend::GetInstrType(instr.code);
    string_buffer_->AppendFormat("%.8X %.8X    ", instr.address, instr.code);
    xe::cpu::frontend::DisasmPPC(instr, string_buffer_);
    string_buffer_->Append("\r\n");
  }

  return gcnew String(string_buffer_->ToString());
}

String ^ Disassembler::DisassembleX64(IntPtr code_address, size_t code_size) {
  string_buffer_->Reset();

  auto code_base = reinterpret_cast<const uint8_t*>(code_address.ToPointer());
  auto code_ptr = code_base;
  size_t remaining_code_size = code_size;
  uint64_t address = uint64_t(code_address.ToInt64());
  cs_insn insn = {0};
  while (remaining_code_size &&
         cs_disasm_iter(capstone_handle_, &code_ptr, &remaining_code_size,
                        &address, &insn)) {
    string_buffer_->AppendFormat("%.8X      %-6s %s\n", uint32_t(insn.address),
                                 insn.mnemonic, insn.op_str);
  }

  return gcnew String(string_buffer_->ToString());
}

}  // namespace Native
}  // namespace Debug
}  // namespace Xenia
