/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/sym/symbol_provider.h>

namespace xdb {
namespace sym {

void SymbolProvider::AddSearchPath(const std::string& path) {
  //
}

std::unique_ptr<SymbolDatabase> SymbolProvider::LoadDatabase(
    const std::string& module_path) {
  return nullptr;
}

}  // namespace sym
}  // namespace xdb
