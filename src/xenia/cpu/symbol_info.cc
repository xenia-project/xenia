/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/symbol_info.h"

#include <cstring>

namespace xe {
namespace cpu {

SymbolInfo::SymbolInfo(SymbolType type, Module* module, uint32_t address)
    : type_(type),
      module_(module),
      status_(SymbolStatus::kDefining),
      address_(address),
      name_("") {}

SymbolInfo::~SymbolInfo() = default;

FunctionInfo::FunctionInfo(Module* module, uint32_t address)
    : SymbolInfo(SymbolType::kFunction, module, address),
      end_address_(0),
      behavior_(FunctionBehavior::kDefault),
      function_(nullptr) {
  std::memset(&extern_info_, 0, sizeof(extern_info_));
}

FunctionInfo::~FunctionInfo() = default;

void FunctionInfo::SetupBuiltin(BuiltinHandler handler, void* arg0,
                                void* arg1) {
  behavior_ = FunctionBehavior::kBuiltin;
  builtin_info_.handler = handler;
  builtin_info_.arg0 = arg0;
  builtin_info_.arg1 = arg1;
}

void FunctionInfo::SetupExtern(ExternHandler handler) {
  behavior_ = FunctionBehavior::kExtern;
  extern_info_.handler = handler;
}

VariableInfo::VariableInfo(Module* module, uint32_t address)
    : SymbolInfo(SymbolType::kVariable, module, address) {}

VariableInfo::~VariableInfo() = default;

}  // namespace cpu
}  // namespace xe
