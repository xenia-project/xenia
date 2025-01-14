/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xboxkrnl_modules.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XexCheckExecutablePrivilege_entry(dword_t privilege) {
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
DECLARE_XBOXKRNL_EXPORT1(XexCheckExecutablePrivilege, kModules, kImplemented);

dword_result_t XexGetModuleHandle(std::string module_name,
                                  xe::be<uint32_t>* hmodule_ptr) {
  object_ref<XModule> module;

  if (module_name.empty()) {
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

dword_result_t XexGetModuleHandle_entry(lpstring_t module_name,
                                        lpdword_t hmodule_ptr) {
  return XexGetModuleHandle(module_name ? module_name.value() : "",
                            hmodule_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(XexGetModuleHandle, kModules, kImplemented);

dword_result_t XexGetModuleSection_entry(lpvoid_t hmodule, lpstring_t name,
                                         lpdword_t data_ptr,
                                         lpdword_t size_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto module = XModule::GetFromHModule(kernel_state(), hmodule);
  if (module) {
    uint32_t section_data = 0;
    uint32_t section_size = 0;
    result = module->GetSection(name.value(), &section_data, &section_size);
    if (XSUCCEEDED(result)) {
      *data_ptr = section_data;
      *size_ptr = section_size;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(XexGetModuleSection, kModules, kImplemented);

dword_result_t xeXexLoadImage(
    lpstring_t module_name, dword_t module_flags, dword_t min_version,
    lpdword_t hmodule_ptr,
    const std::function<object_ref<UserModule>()>& load_callback,
    bool isFromMemory) {
  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  if (!hmodule_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint32_t hmodule = 0;
  auto module = kernel_state()->GetModule(module_name.value());
  if (module) {
    if (isFromMemory) {
      // Existing module found; return error status.
      *hmodule_ptr = hmodule;
      return X_STATUS_OBJECT_NAME_COLLISION;
    } else {
      // Existing module found.
      hmodule = module->hmodule_ptr();
      result = X_STATUS_SUCCESS;
    }
  } else {
    // Not found; attempt to load as a user module.
    auto user_module = load_callback();
    if (user_module) {
      kernel_state()->ApplyTitleUpdate(user_module);
      kernel_state()->FinishLoadingUserModule(user_module);
      // Give up object ownership, this reference will be released by the last
      // XexUnloadImage call
      auto user_module_raw = user_module.release();
      hmodule = user_module_raw->hmodule_ptr();
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

dword_result_t XexLoadImage_entry(lpstring_t module_name, dword_t module_flags,
                                  dword_t min_version, lpdword_t hmodule_ptr) {
  auto load_callback = [module_name] {
    return kernel_state()->LoadUserModule(module_name.value());
  };
  return xeXexLoadImage(module_name, module_flags, min_version, hmodule_ptr,
                        load_callback, false);
}
DECLARE_XBOXKRNL_EXPORT1(XexLoadImage, kModules, kImplemented);

dword_result_t XexLoadImageFromMemory_entry(lpdword_t buffer, dword_t size,
                                            lpstring_t module_name,
                                            dword_t module_flags,
                                            dword_t min_version,
                                            lpdword_t hmodule_ptr) {
  auto load_callback = [module_name, buffer, size] {
    return kernel_state()->LoadUserModuleFromMemory(module_name.value(), buffer,
                                                    size);
  };
  return xeXexLoadImage(module_name, module_flags, min_version, hmodule_ptr,
                        load_callback, true);
}
DECLARE_XBOXKRNL_EXPORT1(XexLoadImageFromMemory, kModules, kImplemented);

dword_result_t XexLoadExecutable_entry(lpstring_t module_name,
                                       dword_t module_flags,
                                       dword_t min_version,
                                       lpdword_t hmodule_ptr) {
  return XexLoadImage_entry(module_name, module_flags, min_version,
                            hmodule_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(XexLoadExecutable, kModules, kSketchy);

dword_result_t XexUnloadImage_entry(lpvoid_t hmodule) {
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
      kernel_state()->UnloadUserModule(object_ref<UserModule>(
          reinterpret_cast<UserModule*>(module.release())));
    }
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XexUnloadImage, kModules, kImplemented);

dword_result_t XexGetProcedureAddress_entry(lpvoid_t hmodule, dword_t ordinal,
                                            lpdword_t out_function_ptr) {
  // May be entry point?
  assert_not_zero(ordinal);

  bool is_string_name = (ordinal & 0xFFFF0000) != 0;
  auto string_name =
      reinterpret_cast<const char*>(kernel_memory()->TranslateVirtual(ordinal));

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
      if (is_string_name) {
        XELOGW("ERROR: XexGetProcedureAddress export '{}' in '{}' not found!",
               string_name, module->name());
      } else {
        XELOGW(
            "ERROR: XexGetProcedureAddress ordinal {} (0x{:X}) in '{}' not "
            "found!",
            static_cast<uint32_t>(ordinal), static_cast<uint32_t>(ordinal),
            module->name());
      }
      *out_function_ptr = 0;
      result = X_STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
    }
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(XexGetProcedureAddress, kModules, kImplemented);

void ExRegisterTitleTerminateNotification_entry(
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
DECLARE_XBOXKRNL_EXPORT1(ExRegisterTitleTerminateNotification, kModules,
                         kImplemented);
// todo: replace magic numbers
dword_result_t XexLoadImageHeaders_entry(pointer_t<X_ANSI_STRING> path,
                                         pointer_t<xex2_header> header,
                                         dword_t buffer_size,
                                         const ppc_context_t& ctx) {
  if (buffer_size < 0x800) {
    return X_STATUS_BUFFER_TOO_SMALL;
  }
  auto current_kernel = ctx->kernel_state;
  auto target_path = util::TranslateAnsiPath(current_kernel->memory(), path);

  vfs::File* vfs_file = nullptr;
  vfs::FileAction file_action;
  X_STATUS result = current_kernel->file_system()->OpenFile(
      nullptr, target_path, vfs::FileDisposition::kOpen,
      vfs::FileAccess::kGenericRead, false, true, &vfs_file, &file_action);

  if (!vfs_file) {
    return result;
  }
  size_t bytes_read = 0;

  X_STATUS result_status = vfs_file->ReadSync(
      reinterpret_cast<void*>(header.host_address()), 2048, 0, &bytes_read);

  if (result_status < 0) {
    vfs_file->Destroy();
    return result_status;
  }

  if (header->magic != 'XEX2') {
    vfs_file->Destroy();
    return X_STATUS_INVALID_IMAGE_FORMAT;
  }
  unsigned int header_size = header->header_size;

  if (header_size < 0x800 || header_size > 0x10000 ||
      (header_size & 0x7FF) != 0) {
    result_status = X_STATUS_INVALID_IMAGE_FORMAT;
  } else if (header_size <= buffer_size) {
    if (header_size <= 0x800) {
      result_status = X_STATUS_SUCCESS;
    } else {
      result_status = vfs_file->ReadSync(
          reinterpret_cast<void*>(header.host_address() + 2048),
          header_size - 2048, 2048, &bytes_read);
      if (result_status >= X_STATUS_SUCCESS) {
        result_status = X_STATUS_SUCCESS;
      }
    }

  } else {
    result_status = X_STATUS_BUFFER_TOO_SMALL;
  }

  vfs_file->Destroy();
  return result_status;
}
DECLARE_XBOXKRNL_EXPORT1(XexLoadImageHeaders, kModules, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Module);
