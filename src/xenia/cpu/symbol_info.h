/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SYMBOL_INFO_H_
#define XENIA_CPU_SYMBOL_INFO_H_

#include <cstdint>
#include <string>

#include "xenia/cpu/frontend/ppc_context.h"

namespace xe {
namespace cpu {

class Function;
class Module;

enum class SymbolType {
  kFunction,
  kVariable,
};

enum class SymbolStatus {
  kNew,
  kDeclaring,
  kDeclared,
  kDefining,
  kDefined,
  kFailed,
};

class SymbolInfo {
 public:
  SymbolInfo(SymbolType type, Module* module, uint32_t address);
  virtual ~SymbolInfo();

  SymbolType type() const { return type_; }
  Module* module() const { return module_; }
  SymbolStatus status() const { return status_; }
  void set_status(SymbolStatus value) { status_ = value; }
  uint32_t address() const { return address_; }

  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }

 protected:
  SymbolType type_;
  Module* module_;
  SymbolStatus status_;
  uint32_t address_;

  std::string name_;
};

enum class FunctionBehavior {
  kDefault = 0,
  kProlog,
  kEpilog,
  kEpilogReturn,
  kBuiltin,
  kExtern,
};

class FunctionInfo : public SymbolInfo {
 public:
  FunctionInfo(Module* module, uint32_t address);
  ~FunctionInfo() override;

  bool has_end_address() const { return end_address_ > 0; }
  uint32_t end_address() const { return end_address_; }
  void set_end_address(uint32_t value) { end_address_ = value; }

  FunctionBehavior behavior() const { return behavior_; }
  void set_behavior(FunctionBehavior value) { behavior_ = value; }

  Function* function() const { return function_; }
  void set_function(Function* value) { function_ = value; }

  typedef void (*BuiltinHandler)(frontend::PPCContext* ppc_context, void* arg0,
                                 void* arg1);
  void SetupBuiltin(BuiltinHandler handler, void* arg0, void* arg1);
  BuiltinHandler builtin_handler() const { return builtin_info_.handler; }
  void* builtin_arg0() const { return builtin_info_.arg0; }
  void* builtin_arg1() const { return builtin_info_.arg1; }

  typedef void (*ExternHandler)(frontend::PPCContext* ppc_context,
                                kernel::KernelState* kernel_state);
  void SetupExtern(ExternHandler handler);
  ExternHandler extern_handler() const { return extern_info_.handler; }

 private:
  uint32_t end_address_;
  FunctionBehavior behavior_;
  Function* function_;
  union {
    struct {
      ExternHandler handler;
    } extern_info_;
    struct {
      BuiltinHandler handler;
      void* arg0;
      void* arg1;
    } builtin_info_;
  };
};

class VariableInfo : public SymbolInfo {
 public:
  VariableInfo(Module* module, uint32_t address);
  ~VariableInfo() override;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_SYMBOL_INFO_H_
