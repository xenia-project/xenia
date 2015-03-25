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

namespace xe {
namespace cpu {

class Function;
class Module;

class SymbolInfo {
 public:
  enum Type {
    TYPE_FUNCTION,
    TYPE_VARIABLE,
  };
  enum Status {
    STATUS_NEW,
    STATUS_DECLARING,
    STATUS_DECLARED,
    STATUS_DEFINING,
    STATUS_DEFINED,
    STATUS_FAILED,
  };

 public:
  SymbolInfo(Type type, Module* module, uint32_t address);
  virtual ~SymbolInfo();

  Type type() const { return type_; }
  Module* module() const { return module_; }
  Status status() const { return status_; }
  void set_status(Status value) { status_ = value; }
  uint32_t address() const { return address_; }

  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }

 protected:
  Type type_;
  Module* module_;
  Status status_;
  uint32_t address_;

  std::string name_;
};

class FunctionInfo : public SymbolInfo {
 public:
  enum Behavior {
    BEHAVIOR_DEFAULT = 0,
    BEHAVIOR_PROLOG,
    BEHAVIOR_EPILOG,
    BEHAVIOR_EPILOG_RETURN,
    BEHAVIOR_EXTERN,
  };

 public:
  FunctionInfo(Module* module, uint32_t address);
  ~FunctionInfo() override;

  bool has_end_address() const { return end_address_ > 0; }
  uint32_t end_address() const { return end_address_; }
  void set_end_address(uint32_t value) { end_address_ = value; }

  Behavior behavior() const { return behavior_; }
  void set_behavior(Behavior value) { behavior_ = value; }

  Function* function() const { return function_; }
  void set_function(Function* value) { function_ = value; }

  typedef void (*ExternHandler)(void* context, void* arg0, void* arg1);
  void SetupExtern(ExternHandler handler, void* arg0, void* arg1);
  ExternHandler extern_handler() const { return extern_info_.handler; }
  void* extern_arg0() const { return extern_info_.arg0; }
  void* extern_arg1() const { return extern_info_.arg1; }

 private:
  uint32_t end_address_;
  Behavior behavior_;
  Function* function_;
  struct {
    ExternHandler handler;
    void* arg0;
    void* arg1;
  } extern_info_;
};

class VariableInfo : public SymbolInfo {
 public:
  VariableInfo(Module* module, uint32_t address);
  ~VariableInfo() override;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_SYMBOL_INFO_H_
