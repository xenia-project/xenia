/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/module.h"

#include <fstream>
#include <sstream>

#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/profiling.h"

namespace xe {
namespace cpu {

Module::Module(Processor* processor)
    : processor_(processor), memory_(processor->memory()) {}

Module::~Module() = default;

bool Module::ContainsAddress(uint32_t address) { return true; }

SymbolInfo* Module::LookupSymbol(uint32_t address, bool wait) {
  lock_.lock();
  const auto it = map_.find(address);
  SymbolInfo* symbol_info = it != map_.end() ? it->second : nullptr;
  if (symbol_info) {
    if (symbol_info->status() == SymbolStatus::kDeclaring) {
      // Some other thread is declaring the symbol - wait.
      if (wait) {
        do {
          lock_.unlock();
          // TODO(benvanik): sleep for less time?
          xe::threading::Sleep(std::chrono::microseconds(100));
          lock_.lock();
        } while (symbol_info->status() == SymbolStatus::kDeclaring);
      } else {
        // Immediate request, just return.
        symbol_info = nullptr;
      }
    }
  }
  lock_.unlock();
  return symbol_info;
}

SymbolStatus Module::DeclareSymbol(SymbolType type, uint32_t address,
                                   SymbolInfo** out_symbol_info) {
  *out_symbol_info = nullptr;
  lock_.lock();
  auto it = map_.find(address);
  SymbolInfo* symbol_info = it != map_.end() ? it->second : nullptr;
  SymbolStatus status;
  if (symbol_info) {
    // If we exist but are the wrong type, die.
    if (symbol_info->type() != type) {
      lock_.unlock();
      return SymbolStatus::kFailed;
    }
    // If we aren't ready yet spin and wait.
    if (symbol_info->status() == SymbolStatus::kDeclaring) {
      // Still declaring, so spin.
      do {
        lock_.unlock();
        // TODO(benvanik): sleep for less time?
        xe::threading::Sleep(std::chrono::microseconds(100));
        lock_.lock();
      } while (symbol_info->status() == SymbolStatus::kDeclaring);
    }
    status = symbol_info->status();
  } else {
    // Create and return for initialization.
    switch (type) {
      case SymbolType::kFunction:
        symbol_info = new FunctionInfo(this, address);
        break;
      case SymbolType::kVariable:
        symbol_info = new VariableInfo(this, address);
        break;
    }
    map_[address] = symbol_info;
    list_.emplace_back(symbol_info);
    status = SymbolStatus::kNew;
  }
  lock_.unlock();
  *out_symbol_info = symbol_info;

  // Get debug info from providers, if this is new.
  if (status == SymbolStatus::kNew) {
    // TODO(benvanik): lookup in map data/dwarf/etc?
  }

  return status;
}

SymbolStatus Module::DeclareFunction(uint32_t address,
                                     FunctionInfo** out_symbol_info) {
  SymbolInfo* symbol_info;
  SymbolStatus status =
      DeclareSymbol(SymbolType::kFunction, address, &symbol_info);
  *out_symbol_info = (FunctionInfo*)symbol_info;
  return status;
}

SymbolStatus Module::DeclareVariable(uint32_t address,
                                     VariableInfo** out_symbol_info) {
  SymbolInfo* symbol_info;
  SymbolStatus status =
      DeclareSymbol(SymbolType::kVariable, address, &symbol_info);
  *out_symbol_info = (VariableInfo*)symbol_info;
  return status;
}

SymbolStatus Module::DefineSymbol(SymbolInfo* symbol_info) {
  lock_.lock();
  SymbolStatus status;
  if (symbol_info->status() == SymbolStatus::kDeclared) {
    // Declared but undefined, so request caller define it.
    symbol_info->set_status(SymbolStatus::kDefining);
    status = SymbolStatus::kNew;
  } else if (symbol_info->status() == SymbolStatus::kDefining) {
    // Still defining, so spin.
    do {
      lock_.unlock();
      // TODO(benvanik): sleep for less time?
      xe::threading::Sleep(std::chrono::microseconds(100));
      lock_.lock();
    } while (symbol_info->status() == SymbolStatus::kDefining);
    status = symbol_info->status();
  } else {
    status = symbol_info->status();
  }
  lock_.unlock();
  return status;
}

SymbolStatus Module::DefineFunction(FunctionInfo* symbol_info) {
  return DefineSymbol((SymbolInfo*)symbol_info);
}

SymbolStatus Module::DefineVariable(VariableInfo* symbol_info) {
  return DefineSymbol((SymbolInfo*)symbol_info);
}

void Module::ForEachFunction(std::function<void(FunctionInfo*)> callback) {
  std::lock_guard<xe::mutex> guard(lock_);
  for (auto& symbol_info : list_) {
    if (symbol_info->type() == SymbolType::kFunction) {
      FunctionInfo* info = static_cast<FunctionInfo*>(symbol_info.get());
      callback(info);
    }
  }
}

void Module::ForEachSymbol(size_t start_index, size_t end_index,
                           std::function<void(SymbolInfo*)> callback) {
  std::lock_guard<xe::mutex> guard(lock_);
  start_index = std::min(start_index, list_.size());
  end_index = std::min(end_index, list_.size());
  for (size_t i = start_index; i <= end_index; ++i) {
    auto& symbol_info = list_[i];
    callback(symbol_info.get());
  }
}

size_t Module::QuerySymbolCount() {
  std::lock_guard<xe::mutex> guard(lock_);
  return list_.size();
}

bool Module::ReadMap(const char* file_name) {
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
           (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) {
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
      if (!processor_->LookupFunctionInfo(this, address, &fn_info)) {
        continue;
      }
      // Don't overwrite names we've set elsewhere.
      if (fn_info->name().empty()) {
        // If it's a mangled C++ name (?name@...) just use the name.
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

  return true;
}

}  // namespace cpu
}  // namespace xe
