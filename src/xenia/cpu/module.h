/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_MODULE_H_
#define XENIA_CPU_MODULE_H_

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xenia/memory.h"
#include "xenia/cpu/symbol_info.h"

namespace xe {
namespace cpu {

class Function;
class Runtime;

class Module {
 public:
  Module(Runtime* runtime);
  virtual ~Module();

  Memory* memory() const { return memory_; }

  virtual const std::string& name() const = 0;

  virtual bool ContainsAddress(uint64_t address);

  SymbolInfo* LookupSymbol(uint64_t address, bool wait = true);
  virtual SymbolInfo::Status DeclareFunction(uint64_t address,
                                             FunctionInfo** out_symbol_info);
  virtual SymbolInfo::Status DeclareVariable(uint64_t address,
                                             VariableInfo** out_symbol_info);

  SymbolInfo::Status DefineFunction(FunctionInfo* symbol_info);
  SymbolInfo::Status DefineVariable(VariableInfo* symbol_info);

  void ForEachFunction(std::function<void(FunctionInfo*)> callback);
  void ForEachFunction(size_t since, size_t& version,
                       std::function<void(FunctionInfo*)> callback);

  int ReadMap(const char* file_name);

 private:
  SymbolInfo::Status DeclareSymbol(SymbolInfo::Type type, uint64_t address,
                                   SymbolInfo** out_symbol_info);
  SymbolInfo::Status DefineSymbol(SymbolInfo* symbol_info);

 protected:
  Runtime* runtime_;
  Memory* memory_;

 private:
  // TODO(benvanik): replace with a better data structure.
  std::mutex lock_;
  std::unordered_map<uint64_t, SymbolInfo*> map_;
  std::vector<std::unique_ptr<SymbolInfo>> list_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MODULE_H_
