/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_NATIVE_X64_DISASSEMBLER_H_
#define XENIA_DEBUG_NATIVE_X64_DISASSEMBLER_H_

#include <cstdint>

namespace Xenia {
namespace Debug {
namespace Native {

using namespace System;
using namespace System::Text;

public ref class X64Disassembler {
 public:
  X64Disassembler();
  ~X64Disassembler();

  String^ GenerateString(IntPtr code_address, size_t code_size);

 private:
  uintptr_t capstone_handle_;
  StringBuilder^ string_builder_;
};

}  // namespace Native
}  // namespace Debug
}  // namespace Xenia

#endif  // XENIA_DEBUG_NATIVE_X64_DISASSEMBLER_H_
