/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_SYMBOL_INFO_H_
#define ALLOY_RUNTIME_SYMBOL_INFO_H_

#include <alloy/core.h>


namespace alloy {
namespace runtime {

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
  SymbolInfo(Type type, Module* module, uint64_t address);
  virtual ~SymbolInfo();

  Type type() const { return type_; }
  Module* module() const { return module_; }
  Status status() const { return status_; }
  void set_status(Status value) { status_ = value; }
  uint64_t address() const { return address_; }

protected:
  Type      type_;
  Module*   module_;
  Status    status_;
  uint64_t  address_;
};

class FunctionInfo : public SymbolInfo {
public:
  FunctionInfo(Module* module, uint64_t address);
  virtual ~FunctionInfo();

  bool has_end_address() const { return end_address_ > 0; }
  uint64_t end_address() const { return end_address_; }
  void set_end_address(uint64_t value) { end_address_ = value; }

  Function* function() const { return function_; }
  void set_function(Function* value) { function_ = value; }

private:
  uint64_t  end_address_;
  Function* function_;
};

class VariableInfo : public SymbolInfo {
public:
  VariableInfo(Module* module, uint64_t address);
  virtual ~VariableInfo();

private:
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_SYMBOL_INFO_H_
