/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_SCANNER_H_
#define XENIA_FRONTEND_PPC_SCANNER_H_

#include <vector>

#include "xenia/cpu/symbol_info.h"

namespace xe {
namespace cpu {
namespace frontend {

class PPCFrontend;

typedef struct BlockInfo_t {
  uint64_t start_address;
  uint64_t end_address;
} BlockInfo;

class PPCScanner {
 public:
  PPCScanner(PPCFrontend* frontend);
  ~PPCScanner();

  int FindExtents(FunctionInfo* symbol_info);

  std::vector<BlockInfo> FindBlocks(FunctionInfo* symbol_info);

 private:
  bool IsRestGprLr(uint64_t address);

 private:
  PPCFrontend* frontend_;
};

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_SCANNER_H_
