/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_EMIT_H_
#define ALLOY_FRONTEND_PPC_PPC_EMIT_H_

#include <alloy/frontend/ppc/ppc_instr.h>


namespace alloy {
namespace frontend {
namespace ppc {


void RegisterEmitCategoryAltivec();
void RegisterEmitCategoryALU();
void RegisterEmitCategoryControl();
void RegisterEmitCategoryFPU();
void RegisterEmitCategoryMemory();


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_EMIT_H_
