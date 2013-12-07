/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_ob.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/xobject.h>
#include <xenia/kernel/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


SHIM_CALL ObReferenceObjectByHandle_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t object_type_ptr = SHIM_GET_ARG_32(1);
  uint32_t out_object_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "ObReferenceObjectByHandle(%.8X, %.8X, %.8X)",
      handle,
      object_type_ptr,
      out_object_ptr);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  XObject* object = NULL;
  result = state->object_table()->GetObject(handle, &object);
  if (XSUCCEEDED(result)) {
    // TODO(benvanik): verify type with object_type_ptr

    // TODO(benvanik): get native value, if supported.
    uint32_t native_ptr = 0xDEADF00D;
    switch (object_type_ptr) {
    case 0xD01BBEEF: // ExThreadObjectType
      {
        XThread* thread = (XThread*)object;
        native_ptr = thread->thread_state();
      }
      break;
    }

    if (out_object_ptr) {
      SHIM_SET_MEM_32(out_object_ptr, native_ptr);
    }
  }

  SHIM_SET_RETURN(result);
}


SHIM_CALL ObDereferenceObject_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t native_ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "ObDereferenceObject(%.8X)",
      native_ptr);

  // Check if a dummy value from ObReferenceObjectByHandle.
  if (native_ptr == 0xDEADF00D) {
    SHIM_SET_RETURN(0);
    return;
  }

  void* object_ptr = SHIM_MEM_ADDR(native_ptr);
  XObject* object = XObject::GetObject(state, object_ptr);
  if (object) {
    object->Release();
  }

  SHIM_SET_RETURN(0);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterObExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ObReferenceObjectByHandle, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ObDereferenceObject, state);
}
