/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_module.h"

#include <xenia/kernel/xex2.h>

#include "kernel/shim_utils.h"
#include "kernel/modules/xboxkrnl/xboxkrnl.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


// void ExGetXConfigSetting_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
//   // ?
//   SHIM_SET_RETURN(0);
// }


void XexCheckExecutablePrivilege_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // DWORD Privilege

  uint32_t privilege = SHIM_GET_ARG_32(0);

  XELOGD(
      XT("XexCheckExecutablePrivilege(%.8X)"),
      privilege);

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  // TODO(benvanik): pull from xex header:
  // XEKernelModuleRef module = XEKernelGetExecutableModule(XEGetKernel());
  // const XEXHeader* xexhdr = XEKernelModuleGetXEXHeader(module);
  // return xexhdr->systemFlags & mask;

  if (mask == XEX_SYSTEM_PAL50_INCOMPATIBLE) {
    // Only one we've seen.
  } else {
    XELOGW(XT("XexCheckExecutablePrivilege: %.8X is NOT IMPLEMENTED"),
           privilege);
  }

  SHIM_SET_RETURN(0);
}


void XexGetModuleHandle_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // LPCSZ ModuleName
  // LPHMODULE ModuleHandle

  uint32_t module_name_ptr = SHIM_GET_ARG_32(0);
  const char* module_name = (const char*)SHIM_MEM_ADDR(module_name_ptr);
  uint32_t module_handle_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      XT("XexGetModuleHandle(%s, %.8X)"),
      module_name, module_handle_ptr);

  XEASSERTALWAYS();

  // TODO(benvanik): get module
  // XEKernelModuleRef module = XEKernelGetModuleByName(XEGetKernel(), ModuleName);
  // if (!module) {
    SHIM_SET_RETURN(0);
  //   return;
  // }

  // SHIM_SET_MEM_32(module_handle_ptr, module->handle());
  // SHIM_SET_RETURN(1);
}


}


void xe::kernel::xboxkrnl::RegisterModuleExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  //SHIM_SET_MAPPING(0x00000010, ExGetXConfigSetting_shim, NULL);

  SHIM_SET_MAPPING(0x00000194, XexCheckExecutablePrivilege_shim, NULL);

  SHIM_SET_MAPPING(0x00000195, XexGetModuleHandle_shim, NULL);

  #undef SET_MAPPING
}
