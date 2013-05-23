/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_module.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xbox.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


// SHIM_CALL ExGetXConfigSetting_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
//   // ?
//   SHIM_SET_RETURN(0);
// }


SHIM_CALL XexCheckExecutablePrivilege_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // DWORD Privilege

  uint32_t privilege = SHIM_GET_ARG_32(0);

  XELOGD(
      "XexCheckExecutablePrivilege(%.8X)",
      privilege);

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  XModule* module = state->GetExecutableModule();
  if (!module) {
    SHIM_SET_RETURN(0);
    return;
  }
  xe_xex2_ref xex = module->xex();

  const xe_xex2_header_t* header = xe_xex2_get_header(xex);
  uint32_t result = (header->system_flags & mask) > 0;

  xe_xex2_release(xex);
  module->Release();

  SHIM_SET_RETURN(result);
}


SHIM_CALL XexGetModuleHandle_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // BOOL
  // LPCSZ ModuleName
  // LPHMODULE ModuleHandle

  uint32_t module_name_ptr = SHIM_GET_ARG_32(0);
  const char* module_name = (const char*)SHIM_MEM_ADDR(module_name_ptr);
  uint32_t module_handle_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XexGetModuleHandle(%s, %.8X)",
      module_name, module_handle_ptr);

  XModule* module = state->GetModule(module_name);
  if (!module) {
    SHIM_SET_RETURN(0);
    return;
  }

  // NOTE: we don't retain the handle for return.
  SHIM_SET_MEM_32(module_handle_ptr, module->handle());
  SHIM_SET_RETURN(1);

  module->Release();
}


// SHIM_CALL XexGetModuleSection_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
// }


// SHIM_CALL XexGetProcedureAddress_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
// }


}


void xe::kernel::xboxkrnl::RegisterModuleExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  //SHIM_SET_MAPPING(0x00000010, ExGetXConfigSetting_shim, NULL);

  SHIM_SET_MAPPING(0x00000194, XexCheckExecutablePrivilege_shim, NULL);

  SHIM_SET_MAPPING(0x00000195, XexGetModuleHandle_shim, NULL);
  // SHIM_SET_MAPPING(0x00000196, XexGetModuleSection_shim, NULL);
  // SHIM_SET_MAPPING(0x00000197, XexGetProcedureAddress_shim, NULL);

  #undef SET_MAPPING
}
