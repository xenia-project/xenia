/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xuser_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

X_STATUS xeExGetXConfigSetting(uint16_t category, uint16_t setting,
                               void* buffer, uint16_t buffer_size,
                               uint16_t* required_size) {
  uint16_t setting_size = 0;
  uint32_t value = 0;

  // TODO(benvanik): have real structs here that just get copied from.
  // http://free60.org/XConfig
  // http://freestyledash.googlecode.com/svn/trunk/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
    case 0x0002:
      // XCONFIG_SECURED_CATEGORY
      switch (setting) {
        case 0x0002:  // XCONFIG_SECURED_AV_REGION
          setting_size = 4;
          value = 0x00001000;  // USA/Canada
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0003:
      // XCONFIG_USER_CATEGORY
      switch (setting) {
        case 0x0001:  // XCONFIG_USER_TIME_ZONE_BIAS
        case 0x0002:  // XCONFIG_USER_TIME_ZONE_STD_NAME
        case 0x0003:  // XCONFIG_USER_TIME_ZONE_DLT_NAME
        case 0x0004:  // XCONFIG_USER_TIME_ZONE_STD_DATE
        case 0x0005:  // XCONFIG_USER_TIME_ZONE_DLT_DATE
        case 0x0006:  // XCONFIG_USER_TIME_ZONE_STD_BIAS
        case 0x0007:  // XCONFIG_USER_TIME_ZONE_DLT_BIAS
          setting_size = 4;
          // TODO(benvanik): get this value.
          value = 0;
          break;
        case 0x0009:  // XCONFIG_USER_LANGUAGE
          setting_size = 4;
          value = 0x00000001;  // English
          break;
        case 0x000A:  // XCONFIG_USER_VIDEO_FLAGS
          setting_size = 4;
          value = 0x00040000;
          break;
        case 0x000C:  // XCONFIG_USER_RETAIL_FLAGS
          setting_size = 4;
          // TODO(benvanik): get this value.
          value = 0;
          break;
        case 0x000E:  // XCONFIG_USER_COUNTRY
          setting_size = 4;
          // TODO(benvanik): get this value.
          value = 0;
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    default:
      assert_unhandled_case(category);
      return X_STATUS_INVALID_PARAMETER_1;
  }

  if (buffer_size < setting_size) {
    return X_STATUS_BUFFER_TOO_SMALL;
  }
  if (!buffer && buffer_size) {
    return X_STATUS_INVALID_PARAMETER_3;
  }

  if (buffer) {
    xe::store_and_swap<uint32_t>(buffer, value);
  }
  if (required_size) {
    *required_size = setting_size;
  }

  return X_STATUS_SUCCESS;
}

SHIM_CALL ExGetXConfigSetting_shim(PPCContext* ppc_state, KernelState* state) {
  uint16_t category = SHIM_GET_ARG_16(0);
  uint16_t setting = SHIM_GET_ARG_16(1);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(2);
  uint16_t buffer_size = SHIM_GET_ARG_16(3);
  uint32_t required_size_ptr = SHIM_GET_ARG_32(4);

  XELOGD("ExGetXConfigSetting(%.4X, %.4X, %.8X, %.4X, %.8X)", category, setting,
         buffer_ptr, buffer_size, required_size_ptr);

  void* buffer = buffer_ptr ? SHIM_MEM_ADDR(buffer_ptr) : NULL;
  uint16_t required_size = 0;
  X_STATUS result = xeExGetXConfigSetting(category, setting, buffer,
                                          buffer_size, &required_size);

  if (required_size_ptr) {
    SHIM_SET_MEM_16(required_size_ptr, required_size);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XexCheckExecutablePrivilege_shim(PPCContext* ppc_state,
                                           KernelState* state) {
  uint32_t privilege = SHIM_GET_ARG_32(0);

  XELOGD("XexCheckExecutablePrivilege(%.8X)", privilege);

  // BOOL
  // DWORD Privilege

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  auto module = state->GetExecutableModule();
  if (!module) {
    SHIM_SET_RETURN_32(0);
    return;
  }
  xe_xex2_ref xex = module->xex();

  const xe_xex2_header_t* header = xe_xex2_get_header(xex);
  uint32_t result = (header->system_flags & mask) > 0;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XexGetModuleHandle_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t module_name_ptr = SHIM_GET_ARG_32(0);
  const char* module_name = (const char*)SHIM_MEM_ADDR(module_name_ptr);
  uint32_t module_handle_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XexGetModuleHandle(%s, %.8X)", module_name, module_handle_ptr);

  object_ref<XModule> module;
  if (!module_name) {
    module = state->GetExecutableModule();
  } else {
    module = state->GetModule(module_name);
  }
  if (!module) {
    SHIM_SET_MEM_32(module_handle_ptr, 0);
    SHIM_SET_RETURN_32(X_ERROR_NOT_FOUND);
    return;
  }

  // NOTE: we don't retain the handle for return.
  SHIM_SET_MEM_32(module_handle_ptr, module->handle());

  SHIM_SET_RETURN_32(X_ERROR_SUCCESS);
}

SHIM_CALL XexGetModuleSection_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t name_ptr = SHIM_GET_ARG_32(1);
  const char* name = (const char*)SHIM_MEM_ADDR(name_ptr);
  uint32_t data_ptr = SHIM_GET_ARG_32(2);
  uint32_t size_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XexGetModuleSection(%.8X, %s, %.8X, %.8X)", handle, name, data_ptr,
         size_ptr);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  XModule* module = nullptr;
  state->object_table()->GetObject(handle, (XObject**)&module);
  if (module) {
    uint32_t section_data = 0;
    uint32_t section_size = 0;
    result = module->GetSection(name, &section_data, &section_size);
    if (XSUCCEEDED(result)) {
      SHIM_SET_MEM_32(data_ptr, section_data);
      SHIM_SET_MEM_32(size_ptr, section_size);
    }
    module->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XexLoadImage_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t module_name_ptr = SHIM_GET_ARG_32(0);
  const char* module_name = (const char*)SHIM_MEM_ADDR(module_name_ptr);
  uint32_t module_flags = SHIM_GET_ARG_32(1);
  uint32_t min_version = SHIM_GET_ARG_32(2);
  uint32_t handle_ptr = SHIM_GET_ARG_32(3);

  XELOGD("XexLoadImage(%s, %.8X, %.8X, %.8X)", module_name, module_flags,
         min_version, handle_ptr);

  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  X_HANDLE module_handle = X_INVALID_HANDLE_VALUE;
  auto module = state->GetModule(module_name);
  if (module) {
    // Existing module found, just add a reference and obtain a handle.
    result = state->object_table()->AddHandle(module.get(), &module_handle);
  } else {
    // Not found; attempt to load as a user module.
    auto user_module = state->LoadUserModule(module_name);
    if (user_module) {
      user_module->RetainHandle();
      module_handle = user_module->handle();
      result = X_STATUS_SUCCESS;
    }
  }
  SHIM_SET_MEM_32(handle_ptr, module_handle);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XexUnloadImage_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t module_handle = SHIM_GET_ARG_32(0);

  XELOGD("XexUnloadImage(%.8X)", module_handle);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  result = state->object_table()->RemoveHandle(module_handle);

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XexGetProcedureAddress_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t module_handle = SHIM_GET_ARG_32(0);
  uint32_t ordinal = SHIM_GET_ARG_32(1);
  uint32_t out_function_ptr = SHIM_GET_ARG_32(2);

  // May be entry point?
  assert_not_zero(ordinal);

  bool is_string_name = (ordinal & 0xFFFF0000) != 0;
  auto string_name = reinterpret_cast<const char*>(SHIM_MEM_ADDR(ordinal));

  if (is_string_name) {
    XELOGD("XexGetProcedureAddress(%.8X, %.8X(%s), %.8X)", module_handle,
           ordinal, string_name, out_function_ptr);
  } else {
    XELOGD("XexGetProcedureAddress(%.8X, %.8X, %.8X)", module_handle, ordinal,
           out_function_ptr);
  }

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  XModule* module = NULL;
  if (!module_handle) {
    module = state->GetExecutableModule().get();
  } else {
    result =
        state->object_table()->GetObject(module_handle, (XObject**)&module);
  }

  if (XSUCCEEDED(result)) {
    uint32_t ptr;
    if (is_string_name) {
      ptr = module->GetProcAddressByName(string_name);
    } else {
      ptr = module->GetProcAddressByOrdinal(ordinal);
    }
    if (ptr) {
      SHIM_SET_MEM_32(out_function_ptr, ptr);
      result = X_STATUS_SUCCESS;
    } else {
      XELOGW("ERROR: XexGetProcedureAddress ordinal not found!");
      SHIM_SET_MEM_32(out_function_ptr, 0);
      result = X_STATUS_DRIVER_ORDINAL_NOT_FOUND;
    }
  }

  if (module) {
    module->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL ExRegisterTitleTerminateNotification_shim(PPCContext* ppc_state,
                                                    KernelState* state) {
  uint32_t registration_ptr = SHIM_GET_ARG_32(0);
  uint32_t create = SHIM_GET_ARG_32(1);

  uint32_t routine = SHIM_MEM_32(registration_ptr + 0);
  uint32_t priority = SHIM_MEM_32(registration_ptr + 4);
  // list entry flink
  // list entry blink

  XELOGD("ExRegisterTitleTerminateNotification(%.8X(%.8X), %.1X)",
         registration_ptr, routine, create);

  if (create) {
    // Adding.
    // TODO(benvanik): add to master list (kernel?).
  } else {
    // Removing.
    // TODO(benvanik): remove from master list.
  }
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterModuleExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ExGetXConfigSetting, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexCheckExecutablePrivilege, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleHandle, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XexGetModuleSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XexLoadImage, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XexUnloadImage, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XexGetProcedureAddress, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", ExRegisterTitleTerminateNotification, state);
}
