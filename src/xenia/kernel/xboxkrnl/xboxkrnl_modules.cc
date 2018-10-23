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
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

DEFINE_bool(xconfig_initial_setup, false,
            "Enable the dashboard initial setup/OOBE");

namespace xe {
namespace kernel {
namespace xboxkrnl {

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
        case 0x0001:                // XCONFIG_SECURED_MAC_ADDRESS (6 bytes)
          return X_STATUS_SUCCESS;  // Just return, easier than setting up code
                                    // for different size configs
        case 0x0002:                // XCONFIG_SECURED_AV_REGION
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

          // 0x40 = dashboard initial setup complete
          value = FLAGS_xconfig_initial_setup ? 0 : 0x40;
          break;
        case 0x000E:  // XCONFIG_USER_COUNTRY
          setting_size = 4;
          // TODO(benvanik): get this value.
          value = 0;
          break;
        case 0x000F:  // XCONFIG_USER_PC_FLAGS (parental control?)
          setting_size = 1;
          value = 0;
          break;
        case 0x0010:  // XCONFIG_USER_SMB_CONFIG (0x100 byte string)
                      // Just set the start of the buffer to 0 so that callers
                      // don't error from an un-inited buffer
          setting_size = 4;
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

dword_result_t ExGetXConfigSetting(word_t category, word_t setting,
                                   lpdword_t buffer_ptr, word_t buffer_size,
                                   lpword_t required_size_ptr) {
  uint16_t required_size = 0;
  X_STATUS result = xeExGetXConfigSetting(category, setting, buffer_ptr,
                                          buffer_size, &required_size);

  if (required_size_ptr) {
    *required_size_ptr = required_size;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(ExGetXConfigSetting,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexCheckExecutablePrivilege(dword_t privilege) {
  // BOOL
  // DWORD Privilege

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  auto module = kernel_state()->GetExecutableModule();
  if (!module) {
    return 0;
  }

  uint32_t flags = 0;
  module->GetOptHeader<uint32_t>(XEX_HEADER_SYSTEM_FLAGS, &flags);

  return (flags & mask) > 0;
}
DECLARE_XBOXKRNL_EXPORT(XexCheckExecutablePrivilege,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexGetModuleHandle(lpstring_t module_name,
                                  lpdword_t hmodule_ptr) {
  object_ref<XModule> module;

  if (!module_name) {
    module = kernel_state()->GetExecutableModule();
  } else {
    module = kernel_state()->GetModule(module_name);
  }

  if (!module) {
    *hmodule_ptr = 0;
    return X_ERROR_NOT_FOUND;
  }

  // NOTE: we don't retain the handle for return.
  *hmodule_ptr = module->hmodule_ptr();

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XexGetModuleHandle,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexGetModuleSection(lpvoid_t hmodule, lpstring_t name,
                                   lpdword_t data_ptr, lpdword_t size_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto module = XModule::GetFromHModule(kernel_state(), hmodule);
  if (module) {
    uint32_t section_data = 0;
    uint32_t section_size = 0;
    result = module->GetSection(name, &section_data, &section_size);
    if (XSUCCEEDED(result)) {
      *data_ptr = section_data;
      *size_ptr = section_size;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(XexGetModuleSection,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexLoadImage(lpstring_t module_name, dword_t module_flags,
                            dword_t min_version, lpdword_t hmodule_ptr) {
  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  uint32_t hmodule = 0;
  auto module = kernel_state()->GetModule(module_name);
  if (module) {
    // Existing module found.
    hmodule = module->hmodule_ptr();
    result = X_STATUS_SUCCESS;
  } else {
    // Not found; attempt to load as a user module.
    auto user_module = kernel_state()->LoadUserModule(module_name);
    if (user_module) {
      user_module->Retain();
      hmodule = user_module->hmodule_ptr();
      result = X_STATUS_SUCCESS;
    }
  }

  // Increment the module's load count.
  if (hmodule) {
    auto ldr_data =
        kernel_memory()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule);
    ldr_data->load_count++;
  }

  *hmodule_ptr = hmodule;

  return result;
}
DECLARE_XBOXKRNL_EXPORT(XexLoadImage,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexUnloadImage(lpvoid_t hmodule) {
  auto module = XModule::GetFromHModule(kernel_state(), hmodule);
  if (!module) {
    return X_STATUS_INVALID_HANDLE;
  }

  // Can't unload kernel modules from user code.
  if (module->module_type() != XModule::ModuleType::kKernelModule) {
    auto ldr_data = hmodule.as<X_LDR_DATA_TABLE_ENTRY*>();
    if (--ldr_data->load_count == 0) {
      // No more references, free it.
      module->Release();
      kernel_state()->object_table()->RemoveHandle(module->handle());
    }
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XexUnloadImage,
                        ExportTag::kImplemented | ExportTag::kModules);

dword_result_t XexGetProcedureAddress(lpvoid_t hmodule, dword_t ordinal,
                                      lpdword_t out_function_ptr) {
  // May be entry point?
  assert_not_zero(ordinal);

  bool is_string_name = (ordinal & 0xFFFF0000) != 0;
  auto string_name = reinterpret_cast<const char*>(
      kernel_memory()->virtual_membase() + ordinal);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  object_ref<XModule> module;
  if (!hmodule) {
    module = kernel_state()->GetExecutableModule();
  } else {
    module = XModule::GetFromHModule(kernel_state(), hmodule);
  }
  if (module) {
    uint32_t ptr;
    if (is_string_name) {
      ptr = module->GetProcAddressByName(string_name);
    } else {
      ptr = module->GetProcAddressByOrdinal(ordinal);
    }
    if (ptr) {
      *out_function_ptr = ptr;
      result = X_STATUS_SUCCESS;
    } else {
      XELOGW("ERROR: XexGetProcedureAddress ordinal not found!");
      *out_function_ptr = 0;
      result = X_STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
    }
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(XexGetProcedureAddress, ExportTag::kImplemented);

void ExRegisterTitleTerminateNotification(
    pointer_t<X_EX_TITLE_TERMINATE_REGISTRATION> reg, dword_t create) {
  if (create) {
    // Adding.
    kernel_state()->RegisterTitleTerminateNotification(
        reg->notification_routine, reg->priority);
  } else {
    // Removing.
    kernel_state()->RemoveTitleTerminateNotification(reg->notification_routine);
  }
}
DECLARE_XBOXKRNL_EXPORT(ExRegisterTitleTerminateNotification,
                        ExportTag::kImplemented);

void RegisterModuleExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
