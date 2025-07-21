/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xbdm/xbdm_misc.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xbdm/xbdm_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/xbox.h"

DECLARE_bool(debug);

namespace xe {
namespace kernel {
namespace xbdm {

dword_result_t DmAllocatePool_entry(dword_t bytes) {
  return kernel_memory()->SystemHeapAlloc(bytes);
};
DECLARE_XBDM_EXPORT1(DmAllocatePool, kDebug, kImplemented);

dword_result_t DmFreePool_entry(lpvoid_t data_ptr) {
  kernel_state()->memory()->SystemHeapFree(data_ptr.guest_address());

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmFreePool, kDebug, kImplemented);

dword_result_t DmGetXboxName_entry(lpstring_t name_ptr,
                                   lpdword_t max_name_size_ptr) {
  if (!name_ptr || !max_name_size_ptr) {
    return X_E_INVALIDARG;
  }

  const size_t xbox_name_size =
      xe::string_util::size_in_bytes(DmXboxName, true);

  if (xbox_name_size > *max_name_size_ptr) {
    return XBDM_BUFFER_TOO_SMALL;
  }

  xe::string_util::copy_truncating(name_ptr, DmXboxName, xbox_name_size);

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetXboxName, kDebug, kImplemented)

dword_result_t DmIsDebuggerPresent_entry() {
  return static_cast<uint32_t>(cvars::debug);
}
DECLARE_XBDM_EXPORT1(DmIsDebuggerPresent, kDebug, kStub);

dword_result_t DmSendNotificationString_entry(lpstring_t notify_string_ptr) {
  if (!notify_string_ptr) {
    return X_E_INVALIDARG;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmSendNotificationString, kDebug, kStub);

uint32_t DmRegisterCommandProcessorEx(char* cmd_handler_name_ptr,
                                      xe::be<uint32_t>* handler_fn,
                                      uint32_t thread) {
  if (!cmd_handler_name_ptr) {
    return X_E_INVALIDARG;
  }

  return XBDM_SUCCESSFUL;
}

dword_result_t DmRegisterCommandProcessor_entry(lpstring_t cmd_handler_name_ptr,
                                                lpdword_t handler_fn) {
  // Return success to prevent some games from crashing
  return DmRegisterCommandProcessorEx(cmd_handler_name_ptr, handler_fn, 0);
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessor, kDebug, kStub);

dword_result_t DmRegisterCommandProcessorEx_entry(
    lpstring_t cmd_handler_name_ptr, lpdword_t handler_fn, dword_t thread) {
  // Return success to prevent some games from stalling
  return DmRegisterCommandProcessorEx(cmd_handler_name_ptr, handler_fn, thread);
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessorEx, kDebug, kStub);

dword_result_t DmCaptureStackBackTrace_entry(dword_t frames_to_capture,
                                             lpvoid_t backtrace_ptr) {
  auto trace_ptr =
      kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          backtrace_ptr);

  if (frames_to_capture > MAX_FRAMES_TO_CAPTURE) {
    return X_E_INVALIDARG;
  }

  std::fill_n(trace_ptr, frames_to_capture, 0);

  std::vector stack_frames =
      std::vector(trace_ptr, trace_ptr + frames_to_capture);

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmCaptureStackBackTrace, kDebug, kStub);

dword_result_t DmWalkLoadedModules_entry(
    lpdword_t walk_modules_ptr, pointer_t<DMN_MODULE_LOAD> module_load_ptr) {
  if (!walk_modules_ptr || !module_load_ptr) {
    return X_E_INVALIDARG;
  }

  module_load_ptr.Zero();

  // Some games will loop forever unless this code is returned
  return XBDM_ENDOFLIST;
}
DECLARE_XBDM_EXPORT1(DmWalkLoadedModules, kDebug, kStub);

dword_result_t DmCloseLoadedModules_entry(lpdword_t walk_modules_ptr) {
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmCloseLoadedModules, kDebug, kStub);

dword_result_t DmMapDevkitDrive_entry(const ppc_context_t& ctx) {
  auto devkit_device =
      std::make_unique<xe::vfs::HostPathDevice>("\\DEVKIT", "devkit", false);

  auto fs = kernel_state()->file_system();

  if (!devkit_device->Initialize()) {
    XELOGE("Unable to scan devkit path");
    return XBDM_UNSUCCESSFUL;
  }

  if (!fs->RegisterDevice(std::move(devkit_device))) {
    XELOGE("Unable to register devkit path");
    return XBDM_UNSUCCESSFUL;
  }

  fs->RegisterSymbolicLink("DEVKIT:", "\\DEVKIT");
  fs->RegisterSymbolicLink("e:", "\\DEVKIT");
  return 0;
}
DECLARE_XBDM_EXPORT1(DmMapDevkitDrive, kDebug, kImplemented);

dword_result_t DmFindPdbSignature_entry(
    lpvoid_t base_address, pointer_t<DM_PDB_SIGNATURE> pdb_signature_ptr) {
  if (!base_address || !pdb_signature_ptr) {
    return X_E_INVALIDARG;
  }

  pdb_signature_ptr.Zero();

  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT1(DmFindPdbSignature, kDebug, kStub);

dword_result_t DmGetXbeInfo_entry(lpstring_t name,
                                  pointer_t<DM_XBE> xbe_info_ptr) {
  if (!xbe_info_ptr) {
    return X_E_INVALIDARG;
  }

  xbe_info_ptr.Zero();

  // TODO(gibbed): 4D5307DC appears to expect this as success?
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetXbeInfo, kDebug, kStub);

dword_result_t DmGetConsoleDebugMemoryStatus_entry(
    lpdword_t memory_config_ptr) {
  if (!memory_config_ptr) {
    return X_E_INVALIDARG;
  }

  *memory_config_ptr = DM_CONSOLEMEMCONFIG_NOADDITIONALMEM;

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetConsoleDebugMemoryStatus, kDebug, kStub);

dword_result_t DmGetSystemInfo_entry(pointer_t<XBDM_SYSTEM_INFO> info) {
  if (!info) {
    return X_E_INVALIDARG;
  }

  info->base_kernel_version.major = info->kernel_version.major = 2;
  info->base_kernel_version.minor = info->kernel_version.minor = 0;
  info->base_kernel_version.qfe = info->kernel_version.qfe = 0;

  info->base_kernel_version.build = kBaseKernelBuildVersion;
  info->kernel_version.build = kernel_state()->GetKernelVersion()->build;

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetSystemInfo, kDebug, kStub);

dword_result_t DmSetMemory_entry(lpvoid_t dest_ptr, dword_t buf_size,
                                 lpvoid_t src_ptr, lpdword_t bytes_written) {
  if (!dest_ptr || !src_ptr || !buf_size) {
    return X_E_INVALIDARG;
  }

  if (bytes_written) {
    *bytes_written = 0;
  }

  const uint32_t dest_guest_address = dest_ptr.guest_address();
  const uint32_t dest_guest_high_address = dest_guest_address + buf_size;

  memory::PageAccess access = memory::PageAccess::kNoAccess;
  BaseHeap* dest_heap_ptr =
      kernel_state()->memory()->LookupHeap(dest_guest_address);

  if (!dest_heap_ptr) {
    return XBDM_UNSUCCESSFUL;
  }

  access = dest_heap_ptr->QueryRangeAccess(dest_guest_address,
                                           dest_guest_high_address);

  if (access == memory::PageAccess::kReadWrite) {
    memcpy(dest_ptr, src_ptr, buf_size);

    if (bytes_written) {
      *bytes_written = static_cast<uint32_t>(buf_size);
    }
  } else {
    XELOGE("DmSetMemory failed with page access {}",
           static_cast<uint32_t>(access));

    return XBDM_UNSUCCESSFUL;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmSetMemory, kDebug, kImplemented);

dword_result_t DmGetMemory_entry(lpvoid_t src_ptr, dword_t buf_size,
                                 lpvoid_t dest_ptr, lpdword_t bytes_written) {
  if (!dest_ptr || !src_ptr || !buf_size) {
    return X_E_INVALIDARG;
  }

  if (bytes_written) {
    *bytes_written = 0;
  }

  const uint32_t dest_guest_address = dest_ptr.guest_address();
  const uint32_t dest_guest_high_address = dest_guest_address + buf_size;

  memory::PageAccess access = memory::PageAccess::kNoAccess;
  BaseHeap* dest_heap_ptr =
      kernel_state()->memory()->LookupHeap(dest_guest_address);

  if (!dest_heap_ptr) {
    return XBDM_UNSUCCESSFUL;
  }

  access = dest_heap_ptr->QueryRangeAccess(dest_guest_address,
                                           dest_guest_high_address);

  if (access == memory::PageAccess::kReadWrite) {
    memcpy(dest_ptr, src_ptr, buf_size);

    if (bytes_written) {
      *bytes_written = static_cast<uint32_t>(buf_size);
    }
  } else {
    XELOGE("DmGetMemory failed with page access {}",
           static_cast<uint32_t>(access));

    return XBDM_UNSUCCESSFUL;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetMemory, kDebug, kImplemented);

dword_result_t DmGetThreadInfoEx_entry(
    dword_t thread_id, pointer_t<DM_THREAD_INFO_EX> thread_info) {
  if (!thread_info) {
    return X_E_INVALIDARG;
  }

  thread_info.Zero();

  auto thread = kernel_state()->GetThreadByID(thread_id);

  if (!thread) {
    return XBDM_UNSUCCESSFUL;
  }

  const uint32_t page_size =
      kernel_state()->memory()->GetPhysicalHeap()->page_size();
  uint32_t thread_info_address =
      kernel_state()->memory()->SystemHeapAlloc(page_size);

  const uint32_t thread_name_size = static_cast<uint32_t>(
      xe::string_util::size_in_bytes(thread->name(), true));

  char* thread_name_ptr =
      kernel_state()->memory()->TranslateVirtual<char*>(thread_info_address);

  xe::string_util::copy_truncating(thread_name_ptr, thread->name().data(),
                                   thread_name_size);

  uint32_t tls_base_address = thread_info_address + thread_name_size;
  uint32_t start_address = tls_base_address + sizeof(uint32_t);
  uint32_t stack_base_address = start_address + sizeof(uint32_t);
  uint32_t stack_limit_address = stack_base_address + sizeof(uint32_t);

  *kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
      tls_base_address) = thread->pcr_ptr();
  *kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
      start_address) = thread->start_address();
  *kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
      stack_base_address) = thread->stack_base();
  *kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
      stack_limit_address) = thread->stack_limit();

  thread_info->size = sizeof(DM_THREAD_INFO_EX);
  thread_info->suspend_count = thread->suspend_count();
  thread_info->priority = thread->priority();
  thread_info->tls_base_ptr = tls_base_address;
  thread_info->start_ptr = start_address;
  thread_info->stack_base_ptr = stack_base_address;
  thread_info->stack_limit_ptr = stack_limit_address;
  thread_info->create_time = thread->creation_time();
  thread_info->stack_slack_space = 0;
  thread_info->thread_name_ptr = thread_info_address;
  thread_info->thread_name_length = thread_name_size;
  thread_info->current_processor = 0;

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetThreadInfoEx, kDebug, kSketchy);

dword_result_t DmGetThreadList_entry(lpdword_t thread_ids_ptr,
                                     lpdword_t thread_ids_size_ptr) {
  if (!thread_ids_ptr || !thread_ids_size_ptr) {
    return X_E_INVALIDARG;
  }

  const uint32_t array_entries = *thread_ids_size_ptr;
  uint32_t array_size = sizeof(uint32_t) * array_entries;

  auto thread_ids = kernel_state()->GetAllThreadIDs();
  uint32_t thread_ids_count = static_cast<uint32_t>(thread_ids.size());

  if (thread_ids_count > array_entries) {
    return XBDM_BUFFER_TOO_SMALL;
  }

  if (thread_ids_count > MAX_FRAMES_TO_CAPTURE) {
    return XBDM_UNSUCCESSFUL;
  }

  std::memset(thread_ids_ptr, 0, array_size);

  for (uint32_t i = 0; const auto thread_id : thread_ids) {
    thread_ids_ptr[i] = thread_id;
    i++;
  }

  *thread_ids_size_ptr = thread_ids_count;

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetThreadList, kDebug, kImplemented);

dword_result_t DmSuspendThread_entry(dword_t thread_id) {
  auto thread = kernel_state()->GetThreadByID(thread_id);

  if (!thread) {
    return XBDM_UNSUCCESSFUL;
  }

  X_STATUS result = thread->Suspend();

  if (XFAILED(result)) {
    return XBDM_UNSUCCESSFUL;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmSuspendThread, kDebug, kImplemented);

dword_result_t DmResumeThread_entry(dword_t thread_id) {
  auto thread = kernel_state()->GetThreadByID(thread_id);

  if (!thread) {
    return XBDM_UNSUCCESSFUL;
  }

  X_STATUS result = thread->Resume();

  if (XFAILED(result)) {
    return XBDM_UNSUCCESSFUL;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmResumeThread, kDebug, kImplemented);

dword_result_t DmOpenNotificationSession_entry(dword_t flags,
                                               lpvoid_t dmn_session_ptr) {
  if (!dmn_session_ptr) {
    return X_E_INVALIDARG;
  }

  *dmn_session_ptr = 0;

  // Prevents 555308C2 from crashing
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmOpenNotificationSession, kDebug, kStub);

dword_result_t DmNotify_entry(lpvoid_t dmn_session_ptr, dword_t notification,
                              lpvoid_t callback_ptr) {
  if (!dmn_session_ptr) {
    return X_E_INVALIDARG;
  }

  // Prevents 555308C2 from crashing
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmNotify, kDebug, kStub);

dword_result_t DmStartProfiling_entry(lpstring_t log_file_name_ptr,
                                      dword_t buffer_size) {
  if (!log_file_name_ptr) {
    return X_E_INVALIDARG;
  }

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmStartProfiling, kDebug, kStub);

dword_result_t DmStopProfiling_entry() { return XBDM_SUCCESSFUL; }
DECLARE_XBDM_EXPORT1(DmStopProfiling, kDebug, kStub);

dword_result_t DmSetProfilingOptions_entry(dword_t flags) {
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmSetProfilingOptions, kDebug, kStub);

dword_result_t DmSetDumpMode_entry(dword_t dump_mode) {
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmSetDumpMode, kDebug, kStub);

dword_result_t DmIsFastCAPEnabled_entry() { return XBDM_UNSUCCESSFUL; }
DECLARE_XBDM_EXPORT1(DmIsFastCAPEnabled, kDebug, kStub);

void __CAP_Start_Profiling_entry(dword_t a1, dword_t a2) {}

DECLARE_XBDM_EXPORT1(__CAP_Start_Profiling, kDebug, kStub);

void __CAP_End_Profiling_entry() {}

DECLARE_XBDM_EXPORT1(__CAP_End_Profiling, kDebug, kStub);

void __CAP_Enter_Function_entry() {}

DECLARE_XBDM_EXPORT1(__CAP_Enter_Function, kDebug, kStub);

void __CAP_Exit_Function_entry() {}

DECLARE_XBDM_EXPORT1(__CAP_Exit_Function, kDebug, kStub);

}  // namespace xbdm
}  // namespace kernel
}  // namespace xe

DECLARE_XBDM_EMPTY_REGISTER_EXPORTS(Misc);
