/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_threading.h"

#include "kernel/shim_utils.h"
#include "kernel/modules/xboxkrnl/xboxkrnl.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


void KeGetCurrentProcessType_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // DWORD

  XELOGD(
      XT("KeGetCurrentProcessType()"));

  SHIM_SET_RETURN(X_PROCTYPE_USER);
}


}


void xe::kernel::xboxkrnl::RegisterThreadingExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x00000066, KeGetCurrentProcessType_shim, NULL);

  #undef SET_MAPPING
}
