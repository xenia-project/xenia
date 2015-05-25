/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

SHIM_CALL ObOpenObjectByName_shim(PPCContext* ppc_state, KernelState* state) {
  // r3 = ptr to info?
  //   +0 = -4
  //   +4 = name ptr
  //   +8 = 0
  // r4 = ExEventObjectType | ExSemaphoreObjectType | ExTimerObjectType
  // r5 = 0
  // r6 = out_ptr (handle?)
  uint32_t obj_attributes_ptr = SHIM_GET_ARG_32(0);
  uint32_t object_type_ptr = SHIM_GET_ARG_32(1);
  uint32_t unk = SHIM_GET_ARG_32(2);
  uint32_t handle_ptr = SHIM_GET_ARG_32(3);

  uint32_t name_str_ptr = SHIM_MEM_32(obj_attributes_ptr + 4);
  X_ANSI_STRING name_str(SHIM_MEM_BASE, name_str_ptr);
  auto name = name_str.to_string();

  XELOGD("ObOpenObjectByName(%.8X(name=%s), %.8X, %.8X, %.8X)",
         obj_attributes_ptr, name.c_str(), object_type_ptr, unk, handle_ptr);

  X_HANDLE handle = X_INVALID_HANDLE_VALUE;
  X_STATUS result = state->object_table()->GetObjectByName(name, &handle);
  if (XSUCCEEDED(result)) {
    SHIM_SET_MEM_32(handle_ptr, handle);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL ObReferenceObjectByHandle_shim(PPCContext* ppc_state,
                                         KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t object_type_ptr = SHIM_GET_ARG_32(1);
  uint32_t out_object_ptr = SHIM_GET_ARG_32(2);

  XELOGD("ObReferenceObjectByHandle(%.8X, %.8X, %.8X)", handle, object_type_ptr,
         out_object_ptr);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  XObject* object = NULL;
  result = state->object_table()->GetObject(handle, &object);
  if (XSUCCEEDED(result)) {
    // TODO(benvanik): verify type with object_type_ptr

    // TODO(benvanik): get native value, if supported.
    uint32_t native_ptr;
    switch (object_type_ptr) {
      case 0x00000000: {  // whatever?
        switch (object->type()) {
          // TODO(benvanik): need to track native_ptr in XObject, allocate as
          // needed?
          /*case XObject::kTypeEvent: {
            XEvent* ev = (XEvent*)object;
          } break;*/
          case XObject::kTypeThread: {
            XThread* thread = (XThread*)object;
            native_ptr = thread->thread_state_ptr();
          } break;
          default: {
            assert_unhandled_case(object->type());
            native_ptr = 0xDEADF00D;
          } break;
        }
      } break;
      case 0xD017BEEF: {  // ExSemaphoreObjectType
        // TODO(benvanik): implement.
        assert_unhandled_case(object_type_ptr);
        native_ptr = 0xDEADF00D;
      } break;
      case 0xD01BBEEF: {  // ExThreadObjectType
        XThread* thread = (XThread*)object;
        native_ptr = thread->thread_state_ptr();
      } break;
      default: {
        assert_unhandled_case(object_type_ptr);
        native_ptr = 0xDEADF00D;
      } break;
    }

    if (out_object_ptr) {
      SHIM_SET_MEM_32(out_object_ptr, native_ptr);
    }
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL ObDereferenceObject_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t native_ptr = SHIM_GET_ARG_32(0);

  XELOGD("ObDereferenceObject(%.8X)", native_ptr);

  // Check if a dummy value from ObReferenceObjectByHandle.
  if (native_ptr == 0xDEADF00D) {
    SHIM_SET_RETURN_32(0);
    return;
  }

  void* object_ptr = SHIM_MEM_ADDR(native_ptr);
  auto object = XObject::GetNativeObject<XObject>(state, object_ptr);
  if (object) {
    object->Release();
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NtDuplicateObject_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);
  uint32_t new_handle_ptr = SHIM_GET_ARG_32(1);
  uint32_t options = SHIM_GET_ARG_32(2);

  XELOGD("NtDuplicateObject(%.8X, %.8X, %.8X)", handle, new_handle_ptr,
         options);

  // NOTE: new_handle_ptr can be zero to just close a handle.
  // NOTE: this function seems to be used to get the current thread handle
  //       (passed handle=-2).
  // Because this function is not like the NT version (with cross process
  // mumble), my guess is that it's just use for getting real handles.
  // So we just fake it and properly reference count but not actually make
  // different handles.

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  XObject* obj = 0;
  result = state->object_table()->GetObject(handle, &obj);
  if (XSUCCEEDED(result)) {
    obj->RetainHandle();
    uint32_t new_handle = obj->handle();
    if (new_handle_ptr) {
      SHIM_SET_MEM_32(new_handle_ptr, new_handle);
    }

    if (options == 1 /* DUPLICATE_CLOSE_SOURCE */) {
      // Always close the source object.
      state->object_table()->RemoveHandle(handle);
    }
    obj->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtClose_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t handle = SHIM_GET_ARG_32(0);

  XELOGD("NtClose(%.8X)", handle);

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  result = state->object_table()->RemoveHandle(handle);

  SHIM_SET_RETURN_32(result);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterObExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", ObOpenObjectByName, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ObReferenceObjectByHandle, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", ObDereferenceObject, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtDuplicateObject, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtClose, state);
}
