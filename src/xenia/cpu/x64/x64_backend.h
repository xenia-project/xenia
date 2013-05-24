/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_X64_X64_BACKEND_H_
#define XENIA_CPU_X64_X64_BACKEND_H_

#include <xenia/common.h>

#include <xenia/cpu/backend.h>


namespace xe {
namespace cpu {
namespace x64 {


class X64Backend : public Backend {
public:
  X64Backend();
  virtual ~X64Backend();

  virtual JIT* CreateJIT(xe_memory_ref memory, sdb::SymbolTable* sym_table);

protected:
};


}  // namespace x64
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_X64_X64_BACKEND_H_
