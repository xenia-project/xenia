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

#include <xenia/kernel/export.h>
#include <xenia/cpu/sdb/symbol.h>


namespace xe {
namespace cpu {
namespace sdb {


class SymbolDatabase {
public:
  SymbolDatabase(xe_memory_ref memory, kernel::ExportResolver* export_resolver);
  virtual ~SymbolDatabase();

  virtual int Analyze();

  ExceptionEntrySymbol* GetOrInsertExceptionEntry(uint32_t address);
  FunctionSymbol* GetOrInsertFunction(uint32_t address);
  VariableSymbol* GetOrInsertVariable(uint32_t address);
  FunctionSymbol* GetFunction(uint32_t address);
  VariableSymbol* GetVariable(uint32_t address);
  Symbol* GetSymbol(uint32_t address);

  int GetAllVariables(std::vector<VariableSymbol*>& variables);
  int GetAllFunctions(std::vector<FunctionSymbol*>& functions);

  void Write(const char* file_name);
  void Dump();
  void DumpFunctionBlocks(FunctionSymbol* fn);

protected:
  typedef std::tr1::unordered_map<uint32_t, Symbol*> SymbolMap;
  typedef std::list<FunctionSymbol*> FunctionList;

  int AnalyzeFunction(FunctionSymbol* fn);
  int CompleteFunctionGraph(FunctionSymbol* fn);
  bool FillHoles();
  int FlushQueue();

  bool IsRestGprLr(uint32_t addr);
  virtual uint32_t GetEntryPoint() = 0;
  virtual bool IsValueInTextRange(uint32_t value) = 0;

  xe_memory_ref   memory_;
  kernel::ExportResolver* export_resolver_;
  size_t          function_count_;
  size_t          variable_count_;
  SymbolMap       symbols_;
  FunctionList    scan_queue_;
};


}  // namespace sdb
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_SDB_SYMBOL_DATABASE_H_
