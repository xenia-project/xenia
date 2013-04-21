/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_SYMBOL_H_
#define XENIA_CPU_SDB_SYMBOL_H_

#include <xenia/core.h>

#include <map>
#include <vector>

#include <xenia/kernel/export.h>


namespace xe {
namespace cpu {
namespace sdb {


class FunctionSymbol;
class VariableSymbol;


class FunctionCall {
public:
  uint32_t        address;
  FunctionSymbol* source;
  FunctionSymbol* target;

  FunctionCall(uint32_t address, FunctionSymbol* source,
               FunctionSymbol* target) :
      address(address), source(source), target(target) {}
};

class VariableAccess {
public:
  uint32_t        address;
  FunctionSymbol* source;
  VariableSymbol* target;

  VariableAccess(uint32_t address, FunctionSymbol* source,
                 VariableSymbol* target) :
      address(address), source(source), target(target) {}
};

class Symbol {
public:
  enum SymbolType {
    Function        = 0,
    Variable        = 1,
    ExceptionEntry  = 2,
  };

  virtual ~Symbol();

  SymbolType    symbol_type;

  const char* name();
  void set_name(const char* value);

protected:
  Symbol(SymbolType type);

  char*   name_;
};

class ExceptionEntrySymbol;

class FunctionBlock {
public:
  enum TargetType {
    kTargetUnknown  = 0,
    kTargetBlock    = 1,
    kTargetFunction = 2,
    kTargetLR       = 3,
    kTargetCTR      = 4,
    kTargetNone     = 5,
  };

  FunctionBlock();

  uint32_t      start_address;
  uint32_t      end_address;

  std::vector<FunctionBlock*> incoming_blocks;

  TargetType        outgoing_type;
  uint32_t          outgoing_address;
  union {
    FunctionSymbol* outgoing_function;
    FunctionBlock*  outgoing_block;
  };
};

class FunctionSymbol : public Symbol {
public:
  enum FunctionType {
    Unknown = 0,
    Kernel  = 1,
    User    = 2,
  };
  enum Flags {
    kFlagSaveGprLr  = 1 << 1,
    kFlagRestGprLr  = 1 << 2,
  };

  FunctionSymbol();
  virtual ~FunctionSymbol();

  FunctionBlock* GetBlock(uint32_t address);
  FunctionBlock* SplitBlock(uint32_t address);

  uint32_t      start_address;
  uint32_t      end_address;
  FunctionType  type;
  uint32_t      flags;

  kernel::KernelExport* kernel_export;
  ExceptionEntrySymbol* ee;

  std::vector<FunctionCall> incoming_calls;
  std::vector<FunctionCall> outgoing_calls;
  std::vector<VariableAccess> variable_accesses;

  std::map<uint32_t, FunctionBlock*> blocks;

  static void AddCall(FunctionSymbol* source, FunctionSymbol* target);
};

class VariableSymbol : public Symbol {
public:
  VariableSymbol();
  virtual ~VariableSymbol();

  uint32_t  address;

  kernel::KernelExport* kernel_export;
};

class ExceptionEntrySymbol : public Symbol {
public:
  ExceptionEntrySymbol();
  virtual ~ExceptionEntrySymbol() {}

  uint32_t  address;
  FunctionSymbol* function;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_SYMBOL_H_
