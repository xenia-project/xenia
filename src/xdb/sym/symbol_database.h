/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_SYM_SYMBOL_DATABASE_H_
#define XDB_SYM_SYMBOL_DATABASE_H_

#include <cstdint>
#include <string>

namespace xdb {
namespace sym {

class ObjectFile {
 public:
  std::string library;
  std::string name;
};

enum class SymbolType {
  FUNCTION,
  VARIABLE,
};

class Symbol {
 public:
  uint32_t address;
  ObjectFile* object_file;
  std::string name;
  SymbolType type;
  bool is_inlined;
};

class SymbolDatabase {
 public:
  virtual Symbol* Lookup(uint32_t address) = 0;

 protected:
  SymbolDatabase() = default;
};

}  // namespace sym
}  // namespace xdb

#endif  // XDB_SYM_SYMBOL_DATABASE_H_
