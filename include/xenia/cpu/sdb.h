/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_H_
#define XENIA_CPU_SDB_H_

#include <xenia/core.h>

#include <list>
#include <map>
#include <vector>

#include <xenia/kernel/export.h>
#include <xenia/kernel/user_module.h>


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
};

class VariableAccess {
public:
  uint32_t        address;
  FunctionSymbol* source;
  VariableSymbol* target;
};

class Symbol {
public:
  enum SymbolType {
    Function        = 0,
    Variable        = 1,
    ExceptionEntry  = 2,
  };

  virtual ~Symbol() {}

  SymbolType    symbol_type;

protected:
  Symbol(SymbolType type) : symbol_type(type) {}
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

  vector<FunctionBlock*> incoming_blocks;

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
  char*         name;
  FunctionType  type;
  uint32_t      flags;

  kernel::KernelExport* kernel_export;
  ExceptionEntrySymbol* ee;

  vector<FunctionCall*> incoming_calls;
  vector<FunctionCall*> outgoing_calls;
  vector<VariableAccess*> variable_accesses;

  map<uint32_t, FunctionBlock*> blocks;
};

class VariableSymbol : public Symbol {
public:
  VariableSymbol();
  virtual ~VariableSymbol();

  uint32_t  address;
  char*     name;
};

class ExceptionEntrySymbol : public Symbol {
public:
  ExceptionEntrySymbol();
  virtual ~ExceptionEntrySymbol() {}

  uint32_t  address;
  FunctionSymbol* function;
};


class SymbolDatabase {
public:
  SymbolDatabase(xe_memory_ref memory, kernel::ExportResolver* export_resolver,
                 kernel::UserModule* module);
  ~SymbolDatabase();

  int Analyze();

  ExceptionEntrySymbol* GetOrInsertExceptionEntry(uint32_t address);
  FunctionSymbol* GetOrInsertFunction(uint32_t address);
  VariableSymbol* GetOrInsertVariable(uint32_t address);
  FunctionSymbol* GetFunction(uint32_t address);
  VariableSymbol* GetVariable(uint32_t address);
  Symbol* GetSymbol(uint32_t address);

  int GetAllFunctions(vector<FunctionSymbol*>& functions);

  void Dump();
  void DumpFunctionBlocks(FunctionSymbol* fn);

private:
  typedef std::map<uint32_t, Symbol*> SymbolMap;
  typedef std::list<FunctionSymbol*> FunctionList;

  int FindGplr();
  int AddImports(const xe_xex2_import_library_t *library);
  int AddMethodHints();
  int AnalyzeFunction(FunctionSymbol* fn);
  int CompleteFunctionGraph(FunctionSymbol* fn);
  bool FillHoles();
  int FlushQueue();

  bool IsValueInTextRange(uint32_t value);
  bool IsRestGprLr(uint32_t addr);

  xe_memory_ref   memory_;
  kernel::ExportResolver* export_resolver_;
  kernel::UserModule* module_;
  size_t          function_count_;
  size_t          variable_count_;
  SymbolMap       symbols_;
  FunctionList    scan_queue_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_H_
