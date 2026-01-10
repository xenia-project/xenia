/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/module.h"

#include <algorithm>
#include <fstream>
#include <sstream>  // NOLINT(readability/streams): should be replaced.
#include <string>

#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

Module::Module(Processor* processor)
    : processor_(processor), memory_(processor->memory()) {}

Module::~Module() = default;

bool Module::ContainsAddress(uint32_t address) { return true; }

Symbol* Module::LookupSymbol(uint32_t address, bool wait) {
  auto global_lock = global_critical_region_.Acquire();
  const auto it = map_.find(address);
  Symbol* symbol = it != map_.end() ? it->second : nullptr;
  if (symbol) {
    if (symbol->status() == Symbol::Status::kDeclaring) {
      // Some other thread is declaring the symbol - wait.
      if (wait) {
        do {
          global_lock.unlock();
          // TODO(benvanik): sleep for less time?
          xe::threading::Sleep(std::chrono::microseconds(100));
          global_lock.lock();
        } while (symbol->status() == Symbol::Status::kDeclaring);
      } else {
        // Immediate request, just return.
        symbol = nullptr;
      }
    }
  }
  global_lock.unlock();
  return symbol;
}

Symbol::Status Module::DeclareSymbol(Symbol::Type type, uint32_t address,
                                     Symbol** out_symbol) {
  *out_symbol = nullptr;
  auto global_lock = global_critical_region_.Acquire();
  auto it = map_.find(address);
  Symbol* symbol = it != map_.end() ? it->second : nullptr;
  Symbol::Status status;
  if (symbol) {
    // If we exist but are the wrong type, die.
    if (symbol->type() != type) {
      global_lock.unlock();
      return Symbol::Status::kFailed;
    }
    // If we aren't ready yet spin and wait.
    if (symbol->status() == Symbol::Status::kDeclaring) {
      // Still declaring, so spin.
      do {
        global_lock.unlock();
        // TODO(benvanik): sleep for less time?
        xe::threading::Sleep(std::chrono::microseconds(100));
        global_lock.lock();
      } while (symbol->status() == Symbol::Status::kDeclaring);
    }
    status = symbol->status();
  } else {
    // Create and return for initialization.
    switch (type) {
      case Symbol::Type::kFunction:
        symbol = CreateFunction(address).release();
        break;
      case Symbol::Type::kVariable:
        symbol = new Symbol(Symbol::Type::kVariable, this, address);
        break;
    }
    map_[address] = symbol;
    list_.emplace_back(symbol);
    status = Symbol::Status::kNew;
  }
  global_lock.unlock();
  *out_symbol = symbol;

  // Get debug info from providers, if this is new.
  if (status == Symbol::Status::kNew) {
    // TODO(benvanik): lookup in map data/dwarf/etc?
  }

  return status;
}

Symbol::Status Module::DeclareFunction(uint32_t address,
                                       Function** out_function) {
  Symbol* symbol;
  Symbol::Status status =
      DeclareSymbol(Symbol::Type::kFunction, address, &symbol);
  *out_function = static_cast<Function*>(symbol);
  return status;
}

Symbol::Status Module::DeclareVariable(uint32_t address, Symbol** out_symbol) {
  Symbol::Status status =
      DeclareSymbol(Symbol::Type::kVariable, address, out_symbol);
  return status;
}

Symbol::Status Module::DefineSymbol(Symbol* symbol) {
  auto global_lock = global_critical_region_.Acquire();
  Symbol::Status status;
  if (symbol->status() == Symbol::Status::kDeclared) {
    // Declared but undefined, so request caller define it.
    symbol->set_status(Symbol::Status::kDefining);
    status = Symbol::Status::kNew;
  } else if (symbol->status() == Symbol::Status::kDefining) {
    // Still defining, so spin.
    do {
      global_lock.unlock();
      // TODO(benvanik): sleep for less time?
      xe::threading::Sleep(std::chrono::microseconds(100));
      global_lock.lock();
    } while (symbol->status() == Symbol::Status::kDefining);
    status = symbol->status();
  } else {
    status = symbol->status();
  }
  global_lock.unlock();
  return status;
}

Symbol::Status Module::DefineFunction(Function* symbol) {
  return DefineSymbol(symbol);
}

Symbol::Status Module::DefineVariable(Symbol* symbol) {
  return DefineSymbol(symbol);
}

const std::vector<uint32_t> Module::GetAddressedFunctions() {
  std::vector<uint32_t> addresses;

  for (const auto& [key, _] : map_) {
    addresses.push_back(key);
  }
  return addresses;
}

void Module::ForEachFunction(std::function<void(Function*)> callback) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto& symbol : list_) {
    if (symbol->type() == Symbol::Type::kFunction) {
      Function* info = static_cast<Function*>(symbol.get());
      callback(info);
    }
  }
}

void Module::ForEachSymbol(size_t start_index, size_t end_index,
                           std::function<void(Symbol*)> callback) {
  auto global_lock = global_critical_region_.Acquire();
  start_index = std::min(start_index, list_.size());
  end_index = std::min(end_index, list_.size());
  for (size_t i = start_index; i <= end_index; ++i) {
    auto& symbol = list_[i];
    callback(symbol.get());
  }
}

size_t Module::QuerySymbolCount() {
  auto global_lock = global_critical_region_.Acquire();
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
      auto function = processor_->LookupFunction(this, address);
      if (!function) {
        continue;
      }
      // Don't overwrite names we've set elsewhere.
      if (function->name().empty()) {
        // If it's a mangled C++ name (?name@...) just use the name.
        // TODO(benvanik): better demangling, or leave it to clients.
        /*if (name[0] == '?') {
          size_t at = name.find('@');
          name = name.substr(1, at - 1);
        }*/
        function->set_name(name.c_str());
      }
    } else {
      // Variable.
    }
  }

  return true;
}

}  // namespace cpu
}  // namespace xe
