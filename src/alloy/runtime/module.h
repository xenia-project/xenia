/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_MODULE_H_
#define ALLOY_RUNTIME_MODULE_H_

#include <alloy/core.h>
#include <alloy/memory.h>
#include <alloy/runtime/symbol_info.h>


namespace alloy {
namespace runtime {

class Function;


class Module {
public:
  Module(Memory* memory);
  virtual ~Module();

  Memory* memory() const { return memory_; }

  virtual const char* name() const = 0;

  virtual bool ContainsAddress(uint64_t address);

  SymbolInfo* LookupSymbol(uint64_t address, bool wait = true);
  SymbolInfo::Status DeclareFunction(
      uint64_t address, FunctionInfo** out_symbol_info);
  SymbolInfo::Status DeclareVariable(
      uint64_t address, VariableInfo** out_symbol_info);

  SymbolInfo::Status DefineFunction(FunctionInfo* symbol_info);
  SymbolInfo::Status DefineVariable(VariableInfo* symbol_info);

private:
  SymbolInfo::Status DeclareSymbol(
      SymbolInfo::Type type, uint64_t address, SymbolInfo** out_symbol_info);
  SymbolInfo::Status DefineSymbol(SymbolInfo* symbol_info);

protected:
  Memory* memory_;

private:
  // TODO(benvanik): replace with a better data structure.
  Mutex* lock_;
  typedef std::tr1::unordered_map<uint64_t, SymbolInfo*> SymbolMap;
  SymbolMap map_;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_MODULE_H_
