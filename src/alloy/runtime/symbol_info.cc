/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/symbol_info.h>

namespace alloy {
namespace runtime {

SymbolInfo::SymbolInfo(Type type, Module* module, uint64_t address)
    : type_(type),
      module_(module),
      status_(STATUS_DEFINING),
      address_(address),
      name_("") {}

SymbolInfo::~SymbolInfo() = default;

FunctionInfo::FunctionInfo(Module* module, uint64_t address)
    : SymbolInfo(SymbolInfo::TYPE_FUNCTION, module, address),
      end_address_(0),
      behavior_(BEHAVIOR_DEFAULT),
      function_(0) {
  memset(&extern_info_, 0, sizeof(extern_info_));
}

FunctionInfo::~FunctionInfo() = default;

void FunctionInfo::SetupExtern(ExternHandler handler, void* arg0, void* arg1) {
  behavior_ = BEHAVIOR_EXTERN;
  extern_info_.handler = handler;
  extern_info_.arg0 = arg0;
  extern_info_.arg1 = arg1;
}

VariableInfo::VariableInfo(Module* module, uint64_t address)
    : SymbolInfo(SymbolInfo::TYPE_VARIABLE, module, address) {}

VariableInfo::~VariableInfo() = default;

}  // namespace runtime
}  // namespace alloy
