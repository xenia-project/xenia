/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_SYM_MAP_SYMBOL_DATABASE_H_
#define XDB_SYM_MAP_SYMBOL_DATABASE_H_

#include <xdb/sym/symbol_database.h>

namespace xdb {
namespace sym {

class MapSymbolDatabase : public SymbolDatabase {
 public:
  Symbol* Lookup(uint32_t address) override;
};

}  // namespace sym
}  // namespace xdb

#endif  // XDB_SYM_MAP_SYMBOL_DATABASE_H_
