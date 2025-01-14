/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_ob.h"
#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/base/utf8.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xsemaphore.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"
namespace xe {
namespace kernel {
namespace xboxkrnl {

void xeObSplitName(X_ANSI_STRING input_string,
                   X_ANSI_STRING* leading_path_component,
                   X_ANSI_STRING* remaining_path_components,
                   PPCContext* context) {
  xe::FatalError("xeObSplitName unimplemented!");
}

uint32_t xeObHashObjectName(X_ANSI_STRING* ElementName, PPCContext* context) {
  uint8_t* current_character_ptr =
      context->TranslateVirtual(ElementName->pointer);
  uint32_t result = 0;
  uint8_t* name_span_end = &current_character_ptr[ElementName->length];
  while (current_character_ptr < name_span_end) {
    uint32_t current_character = *current_character_ptr++;
    if (current_character < 0x80) {
      result = (current_character | 0x20) + (result >> 1) + 3 * result;
    }
  }
  return result % 0xD;
}

uint32_t xeObCreateObject(X_OBJECT_TYPE* object_factory,
                          X_OBJECT_ATTRIBUTES* optional_attributes,
                          uint32_t object_size_without_headers,
                          uint32_t* out_object, cpu::ppc::PPCContext* context) {
  unsigned int resulting_header_flags = 0;
  *out_object = 0;
  unsigned int poolarg = 0;

  auto get_flags_and_poolarg_for_process_type = [&resulting_header_flags,
                                                 &poolarg, context]() {
    uint32_t process_type = xboxkrnl::xeKeGetCurrentProcessType(context);
    if (process_type == X_PROCTYPE_TITLE) {
      poolarg = 1;
      resulting_header_flags = OBJECT_HEADER_IS_TITLE_OBJECT;
    } else {
      poolarg = 2;
    }
  };
  if (!optional_attributes) {
    get_flags_and_poolarg_for_process_type();
  }

  else if ((optional_attributes->attributes & 0x1000) == 0) {
    if ((optional_attributes->attributes & 0x2000) != 0) {
      poolarg = 2;
    } else {
      get_flags_and_poolarg_for_process_type();
    }
  } else {
    poolarg = 1;
    resulting_header_flags = OBJECT_HEADER_IS_TITLE_OBJECT;
  }
  uint32_t desired_object_path_ptr;

  if (!optional_attributes ||
      (desired_object_path_ptr = optional_attributes->name_ptr) == 0) {
    /*
        object has no name provided, just allocate an object with a basic header
    */
    uint64_t allocate_args[] = {
        object_size_without_headers + sizeof(X_OBJECT_HEADER),
        object_factory->pool_tag, poolarg};
    context->processor->Execute(
        context->thread_state, object_factory->allocate_proc, allocate_args, 3);

    uint32_t allocation = static_cast<uint32_t>(context->r[3]);

    if (allocation) {
      X_OBJECT_HEADER* new_object_header =
          context->TranslateVirtual<X_OBJECT_HEADER*>(allocation);
      new_object_header->pointer_count = 1;
      new_object_header->handle_count = 0;
      new_object_header->object_type_ptr =
          context->HostToGuestVirtual(object_factory);
      new_object_header->flags = resulting_header_flags;

      *out_object = allocation + sizeof(X_OBJECT_HEADER);
      return X_STATUS_SUCCESS;
    }
    return X_STATUS_INSUFFICIENT_RESOURCES;
  }
  /*
    iterate through all path components until we obtain the final one, which is
    the objects actual name
  */
  X_ANSI_STRING trailing_path_component;
  X_ANSI_STRING remaining_path;
  X_ANSI_STRING loaded_object_name;
  loaded_object_name =
      *context->TranslateVirtual<X_ANSI_STRING*>(desired_object_path_ptr);
  trailing_path_component.pointer = 0;
  trailing_path_component.length = 0;
  remaining_path = loaded_object_name;
  while (remaining_path.length) {
    xeObSplitName(remaining_path, &trailing_path_component, &remaining_path,
                  context);
    if (remaining_path.length) {
      if (*context->TranslateVirtual<char*>(remaining_path.pointer) == '\\') {
        return X_STATUS_OBJECT_NAME_INVALID;
      }
    }
  }
  if (!trailing_path_component.length) {
    return X_STATUS_OBJECT_NAME_INVALID;
  }
  // the object and its name are all created in a single allocation

  unsigned int aligned_object_size =
      xe::align<uint32_t>(object_size_without_headers, 4);
  {
    uint64_t allocate_args[] = {
        trailing_path_component.length + aligned_object_size +
            sizeof(X_OBJECT_HEADER_NAME_INFO) + sizeof(X_OBJECT_HEADER),
        object_factory->pool_tag, poolarg};

    context->processor->Execute(
        context->thread_state, object_factory->allocate_proc, allocate_args, 3);
  }
  uint32_t named_object_allocation = static_cast<uint32_t>(context->r[3]);
  if (!named_object_allocation) {
    return X_STATUS_INSUFFICIENT_RESOURCES;
  }

  X_OBJECT_HEADER_NAME_INFO* nameinfo =
      context->TranslateVirtual<X_OBJECT_HEADER_NAME_INFO*>(
          named_object_allocation);
  nameinfo->next_in_directory = 0;
  nameinfo->object_directory = 0;

  X_OBJECT_HEADER* header_for_named_object =
      reinterpret_cast<X_OBJECT_HEADER*>(nameinfo + 1);

  char* name_string_memory_for_named_object = &reinterpret_cast<char*>(
      header_for_named_object + 1)[aligned_object_size];
  nameinfo->name.pointer =
      context->HostToGuestVirtual(name_string_memory_for_named_object);
  nameinfo->name.length = trailing_path_component.length;
  nameinfo->name.maximum_length = trailing_path_component.length;

  memcpy(name_string_memory_for_named_object,
         context->TranslateVirtual<char*>(trailing_path_component.pointer),
         trailing_path_component.length);

  header_for_named_object->pointer_count = 1;
  header_for_named_object->handle_count = 0;
  header_for_named_object->object_type_ptr =
      context->HostToGuestVirtual(object_factory);
  header_for_named_object->flags =
      resulting_header_flags & 0xFFFE | OBJECT_HEADER_FLAG_NAMED_OBJECT;
  *out_object = context->HostToGuestVirtual(&header_for_named_object[1]);
  return X_STATUS_SUCCESS;
}
dword_result_t ObOpenObjectByName_entry(lpunknown_t obj_attributes_ptr,
                                        lpunknown_t object_type_ptr,
                                        dword_t unk, lpdword_t handle_ptr) {
  // r3 = ptr to info?
  //   +0 = -4
  //   +4 = name ptr
  //   +8 = 0
  // r4 = ExEventObjectType | ExSemaphoreObjectType | ExTimerObjectType
  // r5 = 0
  // r6 = out_ptr (handle?)

  if (!obj_attributes_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }

  auto obj_attributes = kernel_memory()->TranslateVirtual<X_OBJECT_ATTRIBUTES*>(
      obj_attributes_ptr);
  assert_true(obj_attributes->name_ptr != 0);
  auto name = util::TranslateAnsiStringAddress(kernel_memory(),
                                               obj_attributes->name_ptr);

  X_HANDLE handle = X_INVALID_HANDLE_VALUE;
  X_STATUS result =
      kernel_state()->object_table()->GetObjectByName(name, &handle);
  if (XSUCCEEDED(result)) {
    *handle_ptr = handle;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ObOpenObjectByName, kNone, kImplemented);
// chrispy: investigate this, pretty certain it does not properly emulate the
// original
dword_result_t ObOpenObjectByPointer_entry(lpvoid_t object_ptr,
                                           lpdword_t out_handle_ptr) {
  *out_handle_ptr = 0;
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

dword_result_t ObLookupThreadByThreadId_entry(dword_t thread_id,
                                              lpdword_t out_object_ptr) {
  auto thread = kernel_state()->GetThreadByID(thread_id);
  if (!thread) {
    *out_object_ptr = 0;
    return X_STATUS_INVALID_PARAMETER;
  }

  // Retain the object. Will be released in ObDereferenceObject.
  thread->RetainHandle();
  *out_object_ptr = thread->guest_object();
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObLookupThreadByThreadId, kNone, kImplemented);

dword_result_t ObLookupAnyThreadByThreadId_entry(dword_t thread_id,
                                                 lpdword_t out_object_ptr) {
  return ObLookupThreadByThreadId_entry(thread_id, out_object_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(ObLookupAnyThreadByThreadId, kNone, kImplemented);

dword_result_t ObReferenceObjectByHandle_entry(dword_t handle,
                                               dword_t object_type_ptr,
                                               lpdword_t out_object_ptr) {
  // chrispy: gotta preinit this to 0, kernel is expected to do that
  *out_object_ptr = 0;

  auto object = kernel_state()->object_table()->LookupObject<XObject>(handle);
  if (!object) {
    return X_STATUS_INVALID_HANDLE;
  }

  uint32_t native_ptr = object->guest_object();
  auto& object_types =
      kernel_state()->host_object_type_enum_to_guest_object_type_ptr_;
  auto object_type = object_types.find(object->type());
  if (object_type != object_types.end()) {
    if (object_type_ptr && object_type_ptr != object_type->second) {
      return X_STATUS_OBJECT_TYPE_MISMATCH;
    }
  } else {
    assert_unhandled_case(object->type());
    native_ptr = 0xDEADF00D;
  }
  // Caller takes the reference.
  // It's released in ObDereferenceObject.
  object->RetainHandle();

  xenia_assert(native_ptr != 0);
  if (out_object_ptr.guest_address()) {
    *out_object_ptr = native_ptr;
  }
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObReferenceObjectByHandle, kNone, kImplemented);

dword_result_t ObReferenceObjectByName_entry(pointer_t<X_ANSI_STRING> name,
                                             dword_t attributes,
                                             dword_t object_type_ptr,
                                             lpvoid_t parse_context,
                                             lpdword_t out_object_ptr,
                                             const ppc_context_t& ctx) {
  X_HANDLE handle = X_INVALID_HANDLE_VALUE;

  char* name_str = ctx.TranslateVirtual<char*>(name->pointer);
  X_STATUS result =
      kernel_state()->object_table()->GetObjectByName(name_str, &handle);
  if (XSUCCEEDED(result)) {
    return ObReferenceObjectByHandle_entry(handle, object_type_ptr,
                                           out_object_ptr);
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ObReferenceObjectByName, kNone, kImplemented);

void xeObDereferenceObject(PPCContext* context, uint32_t native_ptr) {
  // Check if a dummy value from ObReferenceObjectByHandle.
  if (native_ptr == 0xDEADF00D) {
    return;
  }
  if (!native_ptr) {
    XELOGE("Null native ptr in ObDereferenceObject!");
    return;
  }

  auto object = XObject::GetNativeObject<XObject>(
      kernel_state(), kernel_memory()->TranslateVirtual(native_ptr));
  if (object) {
    object->ReleaseHandle();

  } else {
    if (native_ptr) {
      XELOGW("Unregistered guest object provided to ObDereferenceObject {:08X}",
             native_ptr);
    }
  }
  return;
}

void ObDereferenceObject_entry(dword_t native_ptr, const ppc_context_t& ctx) {
  xeObDereferenceObject(ctx, native_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(ObDereferenceObject, kNone, kImplemented);

void ObReferenceObject_entry(dword_t native_ptr) {
  // Check if a dummy value from ObReferenceObjectByHandle.
  auto object = XObject::GetNativeObject<XObject>(
      kernel_state(), kernel_memory()->TranslateVirtual(native_ptr));
  if (object) {
    object->RetainHandle();
  } else {
    if (native_ptr) {
      XELOGW("Unregistered guest object provided to ObReferenceObject {:08X}",
             native_ptr.value());
    }
  }
  return;
}
DECLARE_XBOXKRNL_EXPORT1(ObReferenceObject, kNone, kImplemented);

dword_result_t ObCreateSymbolicLink_entry(pointer_t<X_ANSI_STRING> path_ptr,
                                          pointer_t<X_ANSI_STRING> target_ptr) {
  auto path = xe::utf8::canonicalize_guest_path(
      util::TranslateAnsiPath(kernel_memory(), path_ptr));
  auto target = xe::utf8::canonicalize_guest_path(
      util::TranslateAnsiPath(kernel_memory(), target_ptr));

  if (xe::utf8::starts_with(path, "\\??\\")) {
    path = path.substr(4);  // Strip the full qualifier
  }

  if (!kernel_state()->file_system()->RegisterSymbolicLink(path, target)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObCreateSymbolicLink, kNone, kImplemented);

dword_result_t ObDeleteSymbolicLink_entry(pointer_t<X_ANSI_STRING> path_ptr) {
  auto path = util::TranslateAnsiPath(kernel_memory(), path_ptr);
  if (!kernel_state()->file_system()->UnregisterSymbolicLink(path)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ObDeleteSymbolicLink, kNone, kImplemented);

dword_result_t NtDuplicateObject_entry(dword_t handle, lpdword_t new_handle_ptr,
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

uint32_t NtClose(uint32_t handle) {
  return kernel_state()->object_table()->ReleaseHandle(handle);
}

dword_result_t NtClose_entry(dword_t handle) { return NtClose(handle); }
DECLARE_XBOXKRNL_EXPORT1(NtClose, kNone, kImplemented);

dword_result_t ObCreateObject_entry(
    pointer_t<X_OBJECT_TYPE> object_factory,
    pointer_t<X_OBJECT_ATTRIBUTES> optional_attributes,
    dword_t object_size_sans_headers, lpdword_t out_object,
    const ppc_context_t& context) {
  uint32_t out_object_tmp = 0;

  uint32_t result =
      xeObCreateObject(object_factory, optional_attributes,
                       object_size_sans_headers, &out_object_tmp, context);
  *out_object = out_object_tmp;
  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ObCreateObject, kNone, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Ob);
