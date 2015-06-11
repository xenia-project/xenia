/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "Xenia.Debug.Native/x64_disassembler.h"

#include "third_party/capstone/include/capstone.h"
#include "third_party/capstone/include/x86.h"

namespace Xenia {
namespace Debug {
namespace Native {

using namespace System;
using namespace System::Text;

X64Disassembler::X64Disassembler()
    : capstone_handle_(0), string_builder_(gcnew StringBuilder(16 * 1024)) {
  uintptr_t capstone_handle;
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle) != CS_ERR_OK) {
    System::Diagnostics::Debug::Fail("Failed to initialize capstone");
    return;
  }
  capstone_handle_ = capstone_handle;
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
}

X64Disassembler::~X64Disassembler() {
  if (capstone_handle_) {
    pin_ptr<uintptr_t> capstone_handle = &capstone_handle_;
    cs_close(capstone_handle);
  }
}

String^ X64Disassembler::GenerateString(IntPtr code_address,
                                        size_t code_size) {
  string_builder_->Clear();

  //

  return string_builder_->ToString();
}

}  // namespace Native
}  // namespace Debug
}  // namespace Xenia
