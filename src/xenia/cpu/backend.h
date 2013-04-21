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

#include <xenia/core/memory.h>


namespace xe {
namespace cpu {


class CodeUnitBuilder;
class FunctionTable;
class JIT;
class LibraryLinker;
class LibraryLoader;


class Backend {
public:
  virtual ~Backend() {}

  virtual CodeUnitBuilder* CreateCodeUnitBuilder() = 0;
  virtual LibraryLinker* CreateLibraryLinker() = 0;
  virtual LibraryLoader* CreateLibraryLoader() = 0;

  virtual JIT* CreateJIT(xe_memory_ref memory, FunctionTable* fn_table) = 0;

protected:
  Backend() {}
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_BACKEND_H_
