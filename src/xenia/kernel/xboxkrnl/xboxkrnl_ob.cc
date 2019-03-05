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
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xsemaphore.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t ObOpenObjectByName(lpunknown_t obj_attributes_ptr,
                                  lpunknown_t object_type_ptr, dword_t unk,
                                  lpdword_t handle_ptr) {
  // r3 = ptr to info?
  //   +0 = -4
  //   +4 = name ptr
  //   +8 = 0
  // r4 = ExEventObjectType | ExSemaphoreObjectType | ExTimerObjectType
  // r5 = 0
  // r6 = out_ptr (handle?)

  auto name =
      X_ANSI_STRING::to_string_indirect(kernel_memory()->virtual_membase(),
                                        obj_attributes_ptr.guest_address() + 4);

  X_HANDLE handle = X_INVALID_HANDLE_VALUE;
  X_STATUS result =
      kernel_state()->object_table()->GetObjectByName(name, &handle);
  if (XSUCCEEDED(result)) {
    *handle_ptr = handle;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ObOpenObjectByName, kNone, kImplemented);

dword_result_t ObOpenObjectByPointer(lpvoid_t object_ptr,
                                     lpdword_t out_handle_ptr) {
  auto object = XObject::GetNativeObject<XObject>(kernel_state(), object_ptr);
  if (!object) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Retain the handle. Will be released in NtClose.
  object->RetainHandle();
  *out_handle_ptr = object->handle();
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObOpenObjectByPointer, kNone, kImplemented);

dword_result_t ObLookupThreadByThreadId(dword_t thread_id,
                                        lpdword_t out_object_ptr) {
  auto thread = kernel_state()->GetThreadByID(thread_id);
  if (!thread) {
    return X_STATUS_NOT_FOUND;
  }

  // Retain the object. Will be released in ObDereferenceObject.
  thread->Retain();
  *out_object_ptr = thread->guest_object();
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObLookupThreadByThreadId, kNone, kImplemented);

dword_result_t ObReferenceObjectByHandle(dword_t handle,
                                         dword_t object_type_ptr,
                                         lpdword_t out_object_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto object = kernel_state()->object_table()->LookupObject<XObject>(handle);
  if (object) {
    // TODO(benvanik): verify type with object_type_ptr

    // TODO(benvanik): get native value, if supported.
    uint32_t native_ptr;
    switch (object_type_ptr) {
      case 0x00000000: {  // whatever?
        switch (object->type()) {
          case XObject::kTypeEvent: {
            assert(object->type() == XObject::kTypeEvent);
            native_ptr = object->guest_object();
            assert_not_zero(native_ptr);
          } break;
          case XObject::kTypeSemaphore: {
            assert(object->type() == XObject::kTypeSemaphore);
            native_ptr = object->guest_object();
            assert_not_zero(native_ptr);
          } break;
          case XObject::kTypeThread: {
            assert(object->type() == XObject::kTypeThread);
            native_ptr = object->guest_object();
            assert_not_zero(native_ptr);
          } break;
          default: {
            assert_unhandled_case(object->type());
            native_ptr = 0xDEADF00D;
          } break;
        }
      } break;
      case 0xD00EBEEF: {  // ExEventObjectType
        assert(object->type() == XObject::kTypeEvent);
        native_ptr = object->guest_object();
        assert_not_zero(native_ptr);
      } break;
      case 0xD017BEEF: {  // ExSemaphoreObjectType
        assert(object->type() == XObject::kTypeSemaphore);
        native_ptr = object->guest_object();
        assert_not_zero(native_ptr);
      } break;
      case 0xD01BBEEF: {  // ExThreadObjectType
        assert(object->type() == XObject::kTypeThread);
        native_ptr = object->guest_object();
        assert_not_zero(native_ptr);
      } break;
      default: {
        assert_unhandled_case(object_type_ptr);
        native_ptr = 0xDEADF00D;
      } break;
    }

    // Caller takes the reference.
    // It's released in ObDereferenceObject.
    object->Retain();
    if (out_object_ptr.guest_address()) {
      *out_object_ptr = native_ptr;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ObReferenceObjectByHandle, kNone, kImplemented);

dword_result_t ObDereferenceObject(dword_t native_ptr) {
  // Check if a dummy value from ObReferenceObjectByHandle.
  if (native_ptr == 0xDEADF00D) {
    return 0;
  }

  auto object = XObject::GetNativeObject<XObject>(
      kernel_state(), kernel_memory()->virtual_membase() + native_ptr);
  if (object) {
    object->Release();
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(ObDereferenceObject, kNone, kImplemented);

dword_result_t ObCreateSymbolicLink(pointer_t<X_ANSI_STRING> path,
                                    pointer_t<X_ANSI_STRING> target) {
  auto path_str = path->to_string(kernel_memory()->virtual_membase());
  auto target_str = target->to_string(kernel_memory()->virtual_membase());
  path_str = filesystem::CanonicalizePath(path_str);
  target_str = filesystem::CanonicalizePath(target_str);

  auto pos = path_str.find("\\??\\");
  if (pos != path_str.npos && pos == 0) {
    path_str = path_str.substr(4);  // Strip the full qualifier
  }

  if (!kernel_state()->file_system()->RegisterSymbolicLink(path_str,
                                                           target_str)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObCreateSymbolicLink, kNone, kImplemented);

dword_result_t ObDeleteSymbolicLink(pointer_t<X_ANSI_STRING> path) {
  auto path_str = path->to_string(kernel_memory()->virtual_membase());
  if (!kernel_state()->file_system()->UnregisterSymbolicLink(path_str)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObDeleteSymbolicLink, kNone, kImplemented);

dword_result_t NtDuplicateObject(dword_t handle, lpdword_t new_handle_ptr,
                                 dword_t options) {
  // NOTE: new_handle_ptr can be zero to just close a handle.
  // NOTE: this function seems to be used to get the current thread handle
  //       (passed handle=-2).
  // This function actually just creates a new handle to the same object.
  // Most games use it to get real handles to the current thread or whatever.

  X_HANDLE new_handle = X_INVALID_HANDLE_VALUE;
  X_STATUS result =
      kernel_state()->object_table()->DuplicateHandle(handle, &new_handle);

  if (new_handle_ptr) {
    *new_handle_ptr = new_handle;
  }

  if (options == 1 /* DUPLICATE_CLOSE_SOURCE */) {
    // Always close the source object.
    kernel_state()->object_table()->RemoveHandle(handle);
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtDuplicateObject, kNone, kImplemented);

dword_result_t NtClose(dword_t handle) {
  return kernel_state()->object_table()->ReleaseHandle(handle);
}
DECLARE_XBOXKRNL_EXPORT1(NtClose, kNone, kImplemented);

void RegisterObExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
