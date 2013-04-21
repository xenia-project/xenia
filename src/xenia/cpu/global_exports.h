/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_GLOBAL_EXPORTS_H_
#define XENIA_CPU_GLOBAL_EXPORTS_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/ppc/state.h>
#include <xenia/cpu/sdb/symbol.h>
#include <xenia/kernel/export.h>


namespace xe {
namespace cpu {


typedef struct {
  void (*XeTrap)(
      xe_ppc_state_t* state, uint32_t cia);
  void (*XeIndirectBranch)(
      xe_ppc_state_t* state, uint64_t target, uint64_t br_ia);
  void (*XeInvalidInstruction)(
      xe_ppc_state_t* state, uint32_t cia, uint32_t data);
  void (*XeAccessViolation)(
      xe_ppc_state_t* state, uint32_t cia, uint64_t ea);
  void (*XeTraceKernelCall)(
      xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
      kernel::KernelExport* kernel_export);
  void (*XeTraceUserCall)(
      xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
      sdb::FunctionSymbol* fn);
  void (*XeTraceInstruction)(
      xe_ppc_state_t* state, uint32_t cia, uint32_t data);
} GlobalExports;


void GetGlobalExports(GlobalExports* global_exports);


}  // cpu
}  // xe


#endif  // XENIA_CPU_GLOBAL_EXPORTS_H_
