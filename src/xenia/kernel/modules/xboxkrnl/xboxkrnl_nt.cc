/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_nt.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


X_STATUS xeNtCreateFile(
    uint32_t* handle_ptr, uint32_t desired_access, void* object_attributes_ptr,
    void* io_status_block_ptr, uint64_t allocation_size, uint32_t file_attributes,
    uint32_t share_access, uint32_t creation_disposition) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  return X_STATUS_NO_SUCH_FILE;
}

SHIM_CALL NtCreateFile_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t desired_access = SHIM_GET_ARG_32(1);
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(2);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(3);
  uint32_t allocation_size_ptr = SHIM_GET_ARG_32(4);
  uint32_t file_attributes = SHIM_GET_ARG_32(5);
  uint32_t share_access = SHIM_GET_ARG_32(6);
  uint32_t creation_disposition = SHIM_GET_ARG_32(7);

  XELOGD(
      "NtCreateFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %d)",
      handle_ptr,
      desired_access,
      object_attributes_ptr,
      io_status_block_ptr,
      allocation_size_ptr,
      file_attributes,
      share_access,
      creation_disposition);

  // object_attributes is:
  //   void *root_directory
  //   _STRING *object_name <- probably initialized by previous RtlInitAnsiString call
  //   uint32_t attributes

  uint64_t allocation_size = 0; // is this correct???
  if (allocation_size_ptr != 0) {
    allocation_size = SHIM_MEM_64(allocation_size_ptr);
  }

  uint32_t handle;
  X_STATUS result = xeNtCreateFile(
      &handle, desired_access, SHIM_MEM_ADDR(object_attributes_ptr), SHIM_MEM_ADDR(io_status_block_ptr),
      allocation_size, file_attributes, share_access, creation_disposition);

  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
  }
  SHIM_SET_RETURN(result);
}

SHIM_CALL NtQueryInformationFile_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  SHIM_SET_RETURN(X_STATUS_NO_SUCH_FILE);
}

SHIM_CALL NtReadFile_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  SHIM_SET_RETURN(X_STATUS_NO_SUCH_FILE);
}

SHIM_CALL NtQueryFullAttributesFile_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  SHIM_SET_RETURN(X_STATUS_NO_SUCH_FILE);
}

SHIM_CALL NtClose_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  SHIM_SET_RETURN(X_STATUS_NO_SUCH_FILE);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterNtExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtClose, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryFullAttributesFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReadFile, state);
}
