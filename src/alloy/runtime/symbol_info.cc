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
    module_(module), address_(address) {
}

SymbolInfo::~SymbolInfo() {
}

FunctionInfo::FunctionInfo(Module* module, uint64_t address) :
    end_address_(0), function_(0),
    SymbolInfo(SymbolInfo::TYPE_FUNCTION, module, address) {
}

FunctionInfo::~FunctionInfo() {
}

VariableInfo::VariableInfo(Module* module, uint64_t address) :
    SymbolInfo(SymbolInfo::TYPE_VARIABLE, module, address) {
}

VariableInfo::~VariableInfo() {
}
