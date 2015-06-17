/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_NATIVE_DISASSEMBLER_H_
#define XENIA_DEBUG_NATIVE_DISASSEMBLER_H_

#include <cstdint>

#include "xenia/base/string_buffer.h"

namespace Xenia {
namespace Debug {
namespace Native {

using namespace System;
using namespace System::Text;

public ref class Disassembler {
 public:
  Disassembler();
  ~Disassembler();

  String^ DisassemblePPC(IntPtr code_address, size_t code_size);
  String^ DisassembleX64(IntPtr code_address, size_t code_size);

 private:
  uintptr_t capstone_handle_;
  xe::StringBuffer* string_buffer_;
};

}  // namespace Native
}  // namespace Debug
}  // namespace Xenia

#endif  // XENIA_DEBUG_NATIVE_DISASSEMBLER_H_
