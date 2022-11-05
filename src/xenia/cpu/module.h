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
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/symbol.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {

class Processor;

class Module {
 public:
  explicit Module(Processor* processor);
  virtual ~Module();

  Memory* memory() const { return memory_; }

  virtual const std::string& name() const = 0;
  virtual bool is_executable() const = 0;

  virtual bool ContainsAddress(uint32_t address);

  Symbol* LookupSymbol(uint32_t address, bool wait = true);
  virtual Symbol::Status DeclareFunction(uint32_t address,
                                         Function** out_function);
  virtual Symbol::Status DeclareVariable(uint32_t address, Symbol** out_symbol);

  Symbol::Status DefineFunction(Function* symbol);
  Symbol::Status DefineVariable(Symbol* symbol);

  const std::vector<uint32_t> GetAddressedFunctions();
  void ForEachFunction(std::function<void(Function*)> callback);
  void ForEachSymbol(size_t start_index, size_t end_index,
                     std::function<void(Symbol*)> callback);
  size_t QuerySymbolCount();

  bool ReadMap(const char* file_name);

  virtual void Precompile() {}
 protected:
  virtual std::unique_ptr<Function> CreateFunction(uint32_t address) = 0;

  Processor* processor_ = nullptr;
  Memory* memory_ = nullptr;

 private:
  Symbol::Status DeclareSymbol(Symbol::Type type, uint32_t address,
                               Symbol** out_symbol);
  Symbol::Status DefineSymbol(Symbol* symbol);

  xe::global_critical_region global_critical_region_;
  // TODO(benvanik): replace with a better data structure.
  std::unordered_map<uint32_t, Symbol*> map_;
  std::vector<std::unique_ptr<Symbol>> list_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_MODULE_H_
