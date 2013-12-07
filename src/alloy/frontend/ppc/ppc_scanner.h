/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_SCANNER_H_
#define ALLOY_FRONTEND_PPC_PPC_SCANNER_H_

#include <alloy/core.h>
#include <alloy/runtime/symbol_info.h>


namespace alloy {
namespace frontend {
namespace ppc {

class PPCFrontend;


class PPCScanner {
public:
  PPCScanner(PPCFrontend* frontend);
  ~PPCScanner();

  int FindExtents(runtime::FunctionInfo* symbol_info);

private:
  bool IsRestGprLr(uint64_t address);

private:
  PPCFrontend* frontend_;
};


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_SCANNER_H_
