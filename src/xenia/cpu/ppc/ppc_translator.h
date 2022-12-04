/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_TRANSLATOR_H_
#define XENIA_CPU_PPC_PPC_TRANSLATOR_H_

#include <memory>

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace ppc {

class PPCFrontend;
class PPCHIRBuilder;
class PPCScanner;

class PPCTranslator {
 public:
  explicit PPCTranslator(PPCFrontend* frontend);
  ~PPCTranslator();

  bool Translate(GuestFunction* function, uint32_t debug_info_flags);
  void DumpHIR(GuestFunction* function, PPCHIRBuilder* builder);
  void Reset();

 private:
  void DumpSource(GuestFunction* function, StringBuffer* string_buffer);

  PPCFrontend* frontend_;
  std::unique_ptr<PPCScanner> scanner_;
  std::unique_ptr<PPCHIRBuilder> builder_;
  std::unique_ptr<compiler::Compiler> compiler_;
  std::unique_ptr<backend::Assembler> assembler_;

  StringBuffer string_buffer_;
};

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_TRANSLATOR_H_
