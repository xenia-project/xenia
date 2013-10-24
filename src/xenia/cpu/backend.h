/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_H_
#define XENIA_CPU_BACKEND_H_

#include <xenia/common.h>

#include <xenia/memory.h>


namespace xe {
namespace cpu {


class JIT;

namespace sdb {
class SymbolTable;
}  // namespace sdb


class Backend {
public:
  virtual ~Backend() {}

  virtual JIT* CreateJIT(xe_memory_ref memory, sdb::SymbolTable* sym_table) = 0;

protected:
  Backend() {}
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_BACKEND_H_
