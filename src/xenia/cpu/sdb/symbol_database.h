/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_SYMBOL_DATABASE_H_
#define XENIA_CPU_SDB_SYMBOL_DATABASE_H_

#include <xenia/core.h>

#include <list>
#include <map>
#include <vector>

#include <xenia/export_resolver.h>
#include <xenia/cpu/sdb/symbol.h>
#include <xenia/cpu/sdb/symbol_table.h>


namespace xe {
namespace cpu {
namespace sdb {


class SymbolDatabase {
public:
  SymbolDatabase(xe_memory_ref memory, ExportResolver* export_resolver,
                 SymbolTable* sym_table);
  virtual ~SymbolDatabase();

  virtual int Analyze();

  Symbol* GetSymbol(uint32_t address);
  ExceptionEntrySymbol* GetOrInsertExceptionEntry(uint32_t address);
  FunctionSymbol* GetOrInsertFunction(
      uint32_t address, FunctionSymbol* opt_call_source = NULL);
  VariableSymbol* GetOrInsertVariable(uint32_t address);
  FunctionSymbol* GetFunction(uint32_t address, bool analyze = false);
  VariableSymbol* GetVariable(uint32_t address);

  int GetAllVariables(std::vector<VariableSymbol*>& variables);
  int GetAllFunctions(std::vector<FunctionSymbol*>& functions);

  void ReadMap(const char* file_name);
  void WriteMap(const char* file_name);
  void Dump(FILE* file);
  void DumpFunctionBlocks(FILE* file, FunctionSymbol* fn);

protected:
  typedef std::map<uint32_t, Symbol*> SymbolMap;
  typedef std::list<FunctionSymbol*> FunctionList;

  int AnalyzeFunction(FunctionSymbol* fn);
  int CompleteFunctionGraph(FunctionSymbol* fn);
  bool FillHoles();
  int FlushQueue();

  bool IsRestGprLr(uint32_t addr);
  virtual uint32_t GetEntryPoint() = 0;
  virtual bool IsValueInTextRange(uint32_t value) = 0;

  xe_memory_ref   memory_;
  ExportResolver* export_resolver_;
  SymbolTable*    sym_table_;
  size_t          function_count_;
  size_t          variable_count_;
  SymbolMap       symbols_;
  FunctionList    scan_queue_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_SYMBOL_DATABASE_H_
