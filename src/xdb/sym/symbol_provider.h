/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_SYM_SYMBOL_PROVIDER_H_
#define XDB_SYM_SYMBOL_PROVIDER_H_

#include <memory>
#include <string>

#include <xdb/sym/symbol_database.h>

namespace xdb {
namespace sym {

class SymbolProvider {
 public:
  SymbolProvider() = default;

  void AddSearchPath(const std::string& path);

  std::unique_ptr<SymbolDatabase> LoadDatabase(const std::string& module_path);
};

}  // namespace sym
}  // namespace xdb

#endif  // XDB_SYM_SYMBOL_PROVIDER_H_
