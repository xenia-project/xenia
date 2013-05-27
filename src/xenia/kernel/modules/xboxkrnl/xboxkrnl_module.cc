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
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


// SHIM_CALL ExGetXConfigSetting_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
//   // ?
//   SHIM_SET_RETURN(0);
// }


int xeXexCheckExecutablePriviledge(uint32_t privilege) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // BOOL
  // DWORD Privilege

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  XModule* module = state->GetExecutableModule();
  if (!module) {
    return 0;
  }
  xe_xex2_ref xex = module->xex();

  const xe_xex2_header_t* header = xe_xex2_get_header(xex);
  uint32_t result = (header->system_flags & mask) > 0;

  xe_xex2_release(xex);
  module->Release();

  return result;
}


SHIM_CALL XexCheckExecutablePrivilege_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t privilege = SHIM_GET_ARG_32(0);

  XELOGD(
      "XexCheckExecutablePrivilege(%.8X)",
      privilege);

  int result = xeXexCheckExecutablePriviledge(privilege);

  SHIM_SET_RETURN(result);
}


int xeXexGetModuleHandle(const char* module_name,
                         X_HANDLE* module_handle_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);

  // BOOL
  // LPCSZ ModuleName
  // LPHMODULE ModuleHandle

  XModule* module = state->GetModule(module_name);
  if (!module) {
    return 0;
  }

  // NOTE: we don't retain the handle for return.
  *module_handle_ptr = module->handle();

  module->Release();

  return 1;
}


SHIM_CALL XexGetModuleHandle_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t module_name_ptr = SHIM_GET_ARG_32(0);
  const char* module_name = (const char*)SHIM_MEM_ADDR(module_name_ptr);
  uint32_t module_handle_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "XexGetModuleHandle(%s, %.8X)",
      module_name, module_handle_ptr);

  X_HANDLE module_handle = 0;
  int result = xeXexGetModuleHandle(module_name, &module_handle);
  if (result) {
    SHIM_SET_MEM_32(module_handle_ptr, module_handle);
  }

  SHIM_SET_RETURN(result);
}


// SHIM_CALL XexGetModuleSection_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
// }


// SHIM_CALL XexGetProcedureAddress_shim(
//     xe_ppc_state_t* ppc_state, KernelState* state) {
// }


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterModuleExports(
    ExportResolver* export_resolver, KernelState* state) {
  // SHIM_SET_MAPPING("xboxkrnl.exe", ExGetXConfigSetting, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexCheckExecutablePrivilege, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleHandle, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleSection, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XexGetProcedureAddress, state);
}
