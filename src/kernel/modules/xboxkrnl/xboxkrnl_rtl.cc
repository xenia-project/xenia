/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_rtl.h"

#include <xenia/kernel/xex2.h>

#include "kernel/shim_utils.h"
#include "kernel/modules/xboxkrnl/xboxkrnl.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


void RtlImageXexHeaderField_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // PVOID
  // PVOID XexHeaderBase
  // DWORD ImageField

  uint32_t xex_header_base    = SHIM_GET_ARG_32(0);
  uint32_t image_field        = SHIM_GET_ARG_32(1);

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XELOGD(
      XT("RtlImageXexHeaderField(%.8X, %.8X)"),
      xex_header_base, image_field);

  if (xex_header_base != 0x80101100) {
    XELOGE(XT("RtlImageXexHeaderField with non-magic base NOT IMPLEMENTED"));
    SHIM_SET_RETURN(0);
    return;
  }

  uint32_t return_value = 0;
  switch (image_field) {
    case XEX_HEADER_DEFAULT_HEAP_SIZE:
      // TODO(benvanik): pull from running module
      // This is header->exe_heap_size.
      //SHIM_SET_MEM_32(0x80101104, [some value]);
      //return_value = 0x80101104;
      return_value = 0;
      break;
    default:
      XELOGE(XT("RtlImageXexHeaderField header field %.8X NOT IMPLEMENTED"),
             image_field);
      SHIM_SET_RETURN(0);
      return;
  }

  SHIM_SET_RETURN(return_value);
}


//RtlInitializeCriticalSection
//RtlEnterCriticalSection
//RtlLeaveCriticalSection

}


void xe::kernel::xboxkrnl::RegisterRtlExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x0000012B, RtlImageXexHeaderField_shim, NULL);

  #undef SET_MAPPING
}
