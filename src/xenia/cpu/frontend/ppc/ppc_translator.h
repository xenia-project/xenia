/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_PPC_TRANSLATOR_H_
#define XENIA_FRONTEND_PPC_PPC_TRANSLATOR_H_

#include <memory>

#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/symbol_info.h"
#include "poly/string_buffer.h"

namespace xe {
namespace cpu {
namespace frontend {
namespace ppc {

class PPCFrontend;
class PPCHIRBuilder;
class PPCScanner;

class PPCTranslator {
 public:
  PPCTranslator(PPCFrontend* frontend);
  ~PPCTranslator();

  int Translate(FunctionInfo* symbol_info, uint32_t debug_info_flags,
                uint32_t trace_flags, Function** out_function);

 private:
  void DumpSource(FunctionInfo* symbol_info, poly::StringBuffer* string_buffer);

 private:
  PPCFrontend* frontend_;
  std::unique_ptr<PPCScanner> scanner_;
  std::unique_ptr<PPCHIRBuilder> builder_;
  std::unique_ptr<compiler::Compiler> compiler_;
  std::unique_ptr<backend::Assembler> assembler_;

  poly::StringBuffer string_buffer_;
};

}  // namespace ppc
}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_PPC_TRANSLATOR_H_
