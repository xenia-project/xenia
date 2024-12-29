/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xbdm/xbdm_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/xbox.h"

// chrispy: no idea what a real valid value is for this
static constexpr const char DmXboxName[] = "Xbox360Name";

namespace xe {
namespace kernel {
namespace xbdm {
#define XBDM_SUCCESSFUL 0x02DA0000
#define XBDM_UNSUCCESSFUL 0x82DA0000

#define MAKE_DUMMY_STUB_PTR(x)             \
  dword_result_t x##_entry() { return 0; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

#define MAKE_DUMMY_STUB_STATUS(x)                                   \
  dword_result_t x##_entry() { return X_STATUS_INVALID_PARAMETER; } \
  DECLARE_XBDM_EXPORT1(x, kDebug, kStub)

dword_result_t DmAllocatePool_entry(dword_t bytes) {
  return kernel_memory()->SystemHeapAlloc(bytes);
};
DECLARE_XBDM_EXPORT1(DmAllocatePool, kDebug, kImplemented);

void DmCloseLoadedModules_entry(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT1(DmCloseLoadedModules, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmFreePool);

dword_result_t DmGetXbeInfo_entry() {
  // TODO(gibbed): 4D5307DC appears to expect this as success?
  // Unknown arguments -- let's hope things don't explode.
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetXbeInfo, kDebug, kStub);

dword_result_t DmGetXboxName_entry(const ppc_context_t& ctx) {
  uint64_t arg1 = ctx->r[3];
  uint64_t arg2 = ctx->r[4];
  if (!arg1 || !arg2) {
    return 0x80070057;
  }
  char* name_out = ctx->TranslateVirtualGPR<char*>(arg1);

  uint32_t* max_name_chars_ptr = ctx->TranslateVirtualGPR<uint32_t*>(arg2);

  uint32_t max_name_chars = xe::load_and_swap<uint32_t>(max_name_chars_ptr);
  strncpy(name_out, DmXboxName, sizeof(DmXboxName));

  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmGetXboxName, kDebug, kImplemented)

dword_result_t DmIsDebuggerPresent_entry() { return 0; }
DECLARE_XBDM_EXPORT1(DmIsDebuggerPresent, kDebug, kStub);

void DmSendNotificationString_entry(lpdword_t unk0_ptr) {}
DECLARE_XBDM_EXPORT1(DmSendNotificationString, kDebug, kStub);

dword_result_t DmRegisterCommandProcessor_entry(lpdword_t name_ptr,
                                                lpdword_t handler_fn) {
  // Return success to prevent some games from crashing
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessor, kDebug, kStub);

dword_result_t DmRegisterCommandProcessorEx_entry(lpdword_t name_ptr,
                                                  lpdword_t handler_fn,
                                                  dword_t unk3) {
  // Return success to prevent some games from stalling
  return XBDM_SUCCESSFUL;
}
DECLARE_XBDM_EXPORT1(DmRegisterCommandProcessorEx, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmStartProfiling);
MAKE_DUMMY_STUB_STATUS(DmStopProfiling);
// two arguments, first is num frames i think, second is some kind of pointer to
// where to capture
dword_result_t DmCaptureStackBackTrace_entry(const ppc_context_t& ctx) {
  uint32_t nframes = static_cast<uint32_t>(ctx->r[3]);
  uint8_t* unknown_addr =
      ctx->TranslateVirtual(static_cast<uint32_t>(ctx->r[4]));
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT1(DmCaptureStackBackTrace, kDebug, kStub);

MAKE_DUMMY_STUB_STATUS(DmGetThreadInfoEx);
MAKE_DUMMY_STUB_STATUS(DmSetProfilingOptions);

dword_result_t DmWalkLoadedModules_entry(lpdword_t unk0_ptr,
                                         lpdword_t unk1_ptr) {
  // Some games will loop forever unless this code is returned
  return 0x82DA0104;
}
DECLARE_XBDM_EXPORT1(DmWalkLoadedModules, kDebug, kStub);

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

dword_result_t DmFindPdbSignature_entry(lpdword_t unk0_ptr,
                                        lpdword_t unk1_ptr) {
  return X_STATUS_INVALID_PARAMETER;
}
DECLARE_XBDM_EXPORT1(DmFindPdbSignature, kDebug, kStub);

dword_result_t DmGetConsoleDebugMemoryStatus_entry() { return XBDM_SUCCESSFUL; }
DECLARE_XBDM_EXPORT1(DmGetConsoleDebugMemoryStatus, kDebug, kStub);

struct XBDM_VERSION_INFO {
  xe::be<uint16_t> major;
  xe::be<uint16_t> minor;
  xe::be<uint16_t> build;
  xe::be<uint16_t> qfe;
};

struct XBDM_SYSTEM_INFO {
  xe::be<uint32_t> size;
  XBDM_VERSION_INFO base_kernel_version;
  XBDM_VERSION_INFO kernel_version;
  XBDM_VERSION_INFO xdk_version;
  xe::be<uint32_t> flags;
};

dword_result_t DmGetSystemInfo_entry(pointer_t<XBDM_SYSTEM_INFO> info) {
  if (!info) {
    return XBDM_UNSUCCESSFUL;
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
    return XBDM_UNSUCCESSFUL;
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
    return XBDM_UNSUCCESSFUL;
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
