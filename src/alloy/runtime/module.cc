/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/module.h>

#include <fstream>
#include <sstream>

#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::runtime;


Module::Module(Runtime* runtime) :
    runtime_(runtime), memory_(runtime->memory()) {
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
    list_.push_back(symbol_info);
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
    status = symbol_info->status();
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

void Module::ForEachFunction(std::function<void (FunctionInfo*)> callback) {
  LockMutex(lock_);
  for (auto it = list_.begin(); it != list_.end(); ++it) {
    SymbolInfo* symbol_info = *it;
    if (symbol_info->type() == SymbolInfo::TYPE_FUNCTION) {
      FunctionInfo* info = (FunctionInfo*)symbol_info;
      callback(info);
    }
  }
  UnlockMutex(lock_);
}

void Module::ForEachFunction(size_t since, size_t& version,
                             std::function<void (FunctionInfo*)> callback) {
  LockMutex(lock_);
  size_t count = list_.size();
  version = count;
  for (size_t n = since; n < count; n++) {
    SymbolInfo* symbol_info = list_[n];
    if (symbol_info->type() == SymbolInfo::TYPE_FUNCTION) {
      FunctionInfo* info = (FunctionInfo*)symbol_info;
      callback(info);
    }
  }
  UnlockMutex(lock_);
}

int Module::ReadMap(const char* file_name) {
  std::ifstream infile(file_name);

  // Skip until '  Address'. Skip the next blank line.
  std::string line;
  while (std::getline(infile, line)) {
    if (line.find("  Address") == 0) {
      // Skip the next line.
      std::getline(infile, line);
      break;
    }
  }

  std::stringstream sstream;
  std::string ignore;
  std::string name;
  std::string addr_str;
  std::string type_str;
  while (std::getline(infile, line)) {
    // Remove newline.
    while (line.size() &&
           (line[line.size() - 1] == '\r' ||
            line[line.size() - 1] == '\n')) {
      line.erase(line.end() - 1);
    }

    // End when we hit the first whitespace.
    if (line.size() == 0) {
      break;
    }

    // Line is [ws][ignore][ws][name][ws][hex addr][ws][(f)][ws][library]
    sstream.clear();
    sstream.str(line);
    sstream >> std::ws;
    sstream >> ignore;
    sstream >> std::ws;
    sstream >> name;
    sstream >> std::ws;
    sstream >> addr_str;
    sstream >> std::ws;
    sstream >> type_str;

    uint32_t address = (uint32_t)strtoul(addr_str.c_str(), NULL, 16);
    if (!address) {
      continue;
    }

    if (type_str == "f") {
      // Function.
      FunctionInfo* fn_info;
      if (runtime_->LookupFunctionInfo(this, address, &fn_info)) {
        continue;
      }
      // Don't overwrite names we've set elsewhere.
      if (!fn_info->name()) {
        // If it's an mangled C++ name (?name@...) just use the name.
        // TODO(benvanik): better demangling, or leave it to clients.
        /*if (name[0] == '?') {
          size_t at = name.find('@');
          name = name.substr(1, at - 1);
        }*/
        fn_info->set_name(name.c_str());
      }
    } else {
      // Variable.
    }
  }

  return 0;
}
