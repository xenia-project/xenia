/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/module.h>

using namespace alloy;
using namespace alloy::runtime;


Module::Module(Memory* memory) :
    memory_(memory) {
  lock_ = AllocMutex(10000);
}

Module::~Module() {
  LockMutex(lock_);
  SymbolMap::iterator it = map_.begin();
  for (; it != map_.end(); ++it) {
    SymbolInfo* symbol_info = it->second;
    delete symbol_info;
  }
  UnlockMutex(lock_);
  FreeMutex(lock_);
}

bool Module::ContainsAddress(uint64_t address) {
  return true;
}

SymbolInfo* Module::LookupSymbol(uint64_t address, bool wait) {
  LockMutex(lock_);
  SymbolMap::const_iterator it = map_.find(address);
  SymbolInfo* symbol_info = it != map_.end() ? it->second : NULL;
  if (symbol_info) {
    if (symbol_info->status() == SymbolInfo::STATUS_DECLARING) {
      // Some other thread is declaring the symbol - wait.
      if (wait) {
        do {
          UnlockMutex(lock_);
          // TODO(benvanik): sleep for less time?
          Sleep(0);
          LockMutex(lock_);
        } while (symbol_info->status() == SymbolInfo::STATUS_DECLARING);
      } else {
        // Immediate request, just return.
        symbol_info = NULL;
      }
    }
  }
  UnlockMutex(lock_);
  return symbol_info;
}

SymbolInfo::Status Module::DeclareSymbol(
    SymbolInfo::Type type, uint64_t address, SymbolInfo** out_symbol_info) {
  *out_symbol_info = NULL;
  LockMutex(lock_);
  SymbolMap::const_iterator it = map_.find(address);
  SymbolInfo* symbol_info = it != map_.end() ? it->second : NULL;
  SymbolInfo::Status status;
  if (symbol_info) {
    // If we exist but are the wrong type, die.
    if (symbol_info->type() != type) {
      UnlockMutex(lock_);
      return SymbolInfo::STATUS_FAILED;
    }
    // If we aren't ready yet spin and wait.
    if (symbol_info->status() == SymbolInfo::STATUS_DECLARING) {
      // Still declaring, so spin.
      do {
        UnlockMutex(lock_);
        // TODO(benvanik): sleep for less time?
        Sleep(0);
        LockMutex(lock_);
      } while (symbol_info->status() == SymbolInfo::STATUS_DECLARING);
    }
    status = symbol_info->status();
  } else {
    // Create and return for initialization.
    switch (type) {
    case SymbolInfo::TYPE_FUNCTION:
      symbol_info = new FunctionInfo(this, address);
      break;
    case SymbolInfo::TYPE_VARIABLE:
      symbol_info = new VariableInfo(this, address);
      break;
    }
    map_[address] = symbol_info;
    status = SymbolInfo::STATUS_NEW;
  }
  UnlockMutex(lock_);
  *out_symbol_info = symbol_info;

  // Get debug info from providers, if this is new.
  if (status == SymbolInfo::STATUS_NEW) {
    // TODO(benvanik): lookup in map data/dwarf/etc?
  }

  return status;
}

SymbolInfo::Status Module::DeclareFunction(
    uint64_t address, FunctionInfo** out_symbol_info) {
  SymbolInfo* symbol_info;
  SymbolInfo::Status status = DeclareSymbol(
      SymbolInfo::TYPE_FUNCTION, address, &symbol_info);
  *out_symbol_info = (FunctionInfo*)symbol_info;
  return status;
}

SymbolInfo::Status Module::DeclareVariable(
    uint64_t address, VariableInfo** out_symbol_info) {
  SymbolInfo* symbol_info;
  SymbolInfo::Status status = DeclareSymbol(
      SymbolInfo::TYPE_VARIABLE, address, &symbol_info);
  *out_symbol_info = (VariableInfo*)symbol_info;
  return status;
}

SymbolInfo::Status Module::DefineSymbol(SymbolInfo* symbol_info) {
  LockMutex(lock_);
  SymbolInfo::Status status;
  if (symbol_info->status() == SymbolInfo::STATUS_DECLARED) {
    // Declared but undefined, so request caller define it.
    symbol_info->set_status(SymbolInfo::STATUS_DEFINING);
    status = SymbolInfo::STATUS_NEW;
  } else if (symbol_info->status() == SymbolInfo::STATUS_DEFINING) {
    // Still defining, so spin.
    do {
      UnlockMutex(lock_);
      // TODO(benvanik): sleep for less time?
      Sleep(0);
      LockMutex(lock_);
    } while (symbol_info->status() == SymbolInfo::STATUS_DEFINING);
  } else {
    status = symbol_info->status();
  }
  UnlockMutex(lock_);
  return status;
}

SymbolInfo::Status Module::DefineFunction(FunctionInfo* symbol_info) {
  return DefineSymbol((SymbolInfo*)symbol_info);
}

SymbolInfo::Status Module::DefineVariable(VariableInfo* symbol_info) {
  return DefineSymbol((SymbolInfo*)symbol_info);
}
