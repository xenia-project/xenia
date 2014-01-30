/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/symbol_info.h>

using namespace alloy;
using namespace alloy::runtime;


SymbolInfo::SymbolInfo(Type type, Module* module, uint64_t address) :
    type_(type), status_(STATUS_DEFINING),
    module_(module), address_(address), name_(0) {
}

SymbolInfo::~SymbolInfo() {
  if (name_) {
    xe_free(name_);
  }
}

void SymbolInfo::set_name(const char* name) {
  if (name_) {
    xe_free(name_);
  }
  name_ = xestrdupa(name);
}

FunctionInfo::FunctionInfo(Module* module, uint64_t address) :
    end_address_(0), behavior_(BEHAVIOR_DEFAULT), function_(0),
    SymbolInfo(SymbolInfo::TYPE_FUNCTION, module, address) {
  xe_zero_struct(&extern_info_, sizeof(extern_info_));
}

FunctionInfo::~FunctionInfo() {
}

void FunctionInfo::SetupExtern(ExternHandler handler, void* arg0, void* arg1) {
  behavior_ = BEHAVIOR_EXTERN;
  extern_info_.handler = handler;
  extern_info_.arg0 = arg0;
  extern_info_.arg1 = arg1;
}

VariableInfo::VariableInfo(Module* module, uint64_t address) :
    SymbolInfo(SymbolInfo::TYPE_VARIABLE, module, address) {
}

VariableInfo::~VariableInfo() {
}
