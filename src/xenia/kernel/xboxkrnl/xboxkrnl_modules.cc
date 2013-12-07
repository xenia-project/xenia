/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_modules.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


X_STATUS xeExGetXConfigSetting(
    uint16_t category, uint16_t setting, void* buffer, uint16_t buffer_size,
    uint16_t* required_size) {
  uint16_t setting_size = 0;
  uint32_t value = 0;

  // TODO(benvanik): have real structs here that just get copied from.
  // http://free60.org/XConfig
  // http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
  case 0x0003:
    // XCONFIG_USER_CATEGORY
    switch (setting) {
    case 0x0001: // XCONFIG_USER_TIME_ZONE_BIAS
    case 0x0002: // XCONFIG_USER_TIME_ZONE_STD_NAME
    case 0x0003: // XCONFIG_USER_TIME_ZONE_DLT_NAME
    case 0x0004: // XCONFIG_USER_TIME_ZONE_STD_DATE
    case 0x0005: // XCONFIG_USER_TIME_ZONE_DLT_DATE
    case 0x0006: // XCONFIG_USER_TIME_ZONE_STD_BIAS
    case 0x0007: // XCONFIG_USER_TIME_ZONE_DLT_BIAS
      setting_size = 4;
      // TODO(benvanik): get this value.
      value = 0;
      break;
    case 0x0009: // XCONFIG_USER_LANGUAGE
      setting_size = 4;
      value = 0x00000001; // English
      break;
    case 0x000A: // XCONFIG_USER_VIDEO_FLAGS
      setting_size = 4;
      value = 0x00040000;
      break;
    case 0x000C: // XCONFIG_USER_RETAIL_FLAGS
      setting_size = 4;
      // TODO(benvanik): get this value.
      value = 0;
      break;
    case 0x000E: // XCONFIG_USER_COUNTRY
      setting_size = 4;
      // TODO(benvanik): get this value.
      value = 0;
      break;
    default:
      XEASSERTALWAYS();
      return X_STATUS_INVALID_PARAMETER_2;
    }
    break;
  default:
    XEASSERTALWAYS();
    return X_STATUS_INVALID_PARAMETER_1;
  }

  if (buffer_size < setting_size) {
    return X_STATUS_BUFFER_TOO_SMALL;
  }
  if (!buffer && buffer_size) {
    return X_STATUS_INVALID_PARAMETER_3;
  }

  if (buffer) {
    XESETUINT32BE(buffer, value);
  }
  if (required_size) {
    *required_size = setting_size;
  }

  return X_STATUS_SUCCESS;
}


SHIM_CALL ExGetXConfigSetting_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint16_t category = SHIM_GET_ARG_16(0);
  uint16_t setting = SHIM_GET_ARG_16(1);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(2);
  uint16_t buffer_size = SHIM_GET_ARG_16(3);
  uint32_t required_size_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "ExGetXConfigSetting(%.4X, %.4X, %.8X, %.4X, %.8X)",
      category, setting, buffer_ptr, buffer_size, required_size_ptr);

  void* buffer = buffer_ptr ? SHIM_MEM_ADDR(buffer_ptr) : NULL;
  uint16_t required_size = 0;
  X_STATUS result = xeExGetXConfigSetting(
      category, setting, buffer, buffer_size, &required_size);

  if (required_size_ptr) {
    SHIM_SET_MEM_16(required_size_ptr, required_size);
  }

  SHIM_SET_RETURN(result);
}


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
    PPCContext* ppc_state, KernelState* state) {
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
    PPCContext* ppc_state, KernelState* state) {
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
//     PPCContext* ppc_state, KernelState* state) {
// }


// SHIM_CALL XexGetProcedureAddress_shim(
//     PPCContext* ppc_state, KernelState* state) {
// }


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterModuleExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ExGetXConfigSetting, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexCheckExecutablePrivilege, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleHandle, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleSection, state);
  // SHIM_SET_MAPPING("xboxkrnl.exe", XexGetProcedureAddress, state);
}
