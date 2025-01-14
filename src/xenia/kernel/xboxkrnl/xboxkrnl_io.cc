/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/info/file.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xiocompletion.h"
#include "xenia/kernel/xsymboliclink.h"
#include "xenia/kernel/xthread.h"
#include "xenia/vfs/device.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

struct CreateOptions {
  // https://processhacker.sourceforge.io/doc/ntioapi_8h.html
  static const uint32_t FILE_DIRECTORY_FILE = 0x00000001;
  // Optimization - files access will be sequential, not random.
  static const uint32_t FILE_SEQUENTIAL_ONLY = 0x00000004;
  static const uint32_t FILE_SYNCHRONOUS_IO_ALERT = 0x00000010;
  static const uint32_t FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020;
  static const uint32_t FILE_NON_DIRECTORY_FILE = 0x00000040;
  // Optimization - file access will be random, not sequential.
  static const uint32_t FILE_RANDOM_ACCESS = 0x00000800;
};

dword_result_t NtCreateFile_entry(lpdword_t handle_out, dword_t desired_access,
                                  pointer_t<X_OBJECT_ATTRIBUTES> object_attrs,
                                  pointer_t<X_IO_STATUS_BLOCK> io_status_block,
                                  lpqword_t allocation_size_ptr,
                                  dword_t file_attributes, dword_t share_access,
                                  dword_t creation_disposition,
                                  dword_t create_options) {
  uint64_t allocation_size = 0;  // is this correct???
  if (allocation_size_ptr) {
    allocation_size = *allocation_size_ptr;
  }

  if (!object_attrs) {
    // ..? Some games do this. This parameter is not optional.
    return X_STATUS_INVALID_PARAMETER;
  }
  assert_not_null(handle_out);

  auto object_name =
      kernel_memory()->TranslateVirtual<X_ANSI_STRING*>(object_attrs->name_ptr);

  vfs::Entry* root_entry = nullptr;

  // Compute path, possibly attrs relative.
  auto target_path = util::TranslateAnsiPath(kernel_memory(), object_name);

  // Enforce that the path is ASCII.
  if (!IsValidPath(target_path, false)) {
    return X_STATUS_OBJECT_NAME_INVALID;
  }

  if (object_attrs->root_directory != 0xFFFFFFFD &&  // ObDosDevices
      object_attrs->root_directory != 0) {
    auto root_file = kernel_state()->object_table()->LookupObject<XFile>(
        object_attrs->root_directory);
    assert_not_null(root_file);
    assert_true(root_file->type() == XObject::Type::File);

    root_entry = root_file->entry();
  }

  // Attempt open (or create).
  vfs::File* vfs_file;
  vfs::FileAction file_action;
  X_STATUS result = kernel_state()->file_system()->OpenFile(
      root_entry, target_path,
      vfs::FileDisposition((uint32_t)creation_disposition), desired_access,
      (create_options & CreateOptions::FILE_DIRECTORY_FILE) != 0,
      (create_options & CreateOptions::FILE_NON_DIRECTORY_FILE) != 0, &vfs_file,
      &file_action);
  object_ref<XFile> file = nullptr;

  X_HANDLE handle = X_INVALID_HANDLE_VALUE;
  if (XSUCCEEDED(result)) {
    // If true, desired_access SYNCHRONIZE flag must be set.
    bool synchronous =
        (create_options & CreateOptions::FILE_SYNCHRONOUS_IO_ALERT) ||
        (create_options & CreateOptions::FILE_SYNCHRONOUS_IO_NONALERT);
    file = object_ref<XFile>(new XFile(kernel_state(), vfs_file, synchronous));

    // Handle ref is incremented, so return that.
    handle = file->handle();
  }

  if (io_status_block) {
    io_status_block->status = result;
    io_status_block->information = (uint32_t)file_action;
  }

  *handle_out = handle;

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateFile, kFileSystem, kImplemented);

dword_result_t NtOpenFile_entry(
    lpdword_t handle_out, dword_t desired_access,
    pointer_t<X_OBJECT_ATTRIBUTES> object_attributes,
    pointer_t<X_IO_STATUS_BLOCK> io_status_block, dword_t open_options) {
  return NtCreateFile_entry(
      handle_out, desired_access, object_attributes, io_status_block, nullptr,
      0, 0, static_cast<uint32_t>(xe::vfs::FileDisposition::kOpen),
      open_options);
}
DECLARE_XBOXKRNL_EXPORT1(NtOpenFile, kFileSystem, kImplemented);

dword_result_t NtReadFile_entry(dword_t file_handle, dword_t event_handle,
                                lpvoid_t apc_routine_ptr, lpvoid_t apc_context,
                                pointer_t<X_IO_STATUS_BLOCK> io_status_block,
                                lpvoid_t buffer, dword_t buffer_length,
                                lpqword_t byte_offset_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  bool signal_event = false;
  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
  if (event_handle && !ev) {
    result = X_STATUS_INVALID_HANDLE;
  }

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (XSUCCEEDED(result)) {
    if (true || file->is_synchronous()) {
      // Synchronous.
      uint32_t bytes_read = 0;
      result = file->Read(
          buffer.guest_address(), buffer_length,
          byte_offset_ptr ? static_cast<uint64_t>(*byte_offset_ptr) : -1,
          &bytes_read, apc_context);
      if (io_status_block) {
        io_status_block->status = result;
        io_status_block->information = bytes_read;
      }

      // Queue the APC callback. It must be delivered via the APC mechanism even
      // though were are completing immediately.
      // Low bit probably means do not queue to IO ports.
      if ((uint32_t)apc_routine_ptr & ~1) {
        if (apc_context) {
          auto thread = XThread::GetCurrentThread();
          thread->EnqueueApc(static_cast<uint32_t>(apc_routine_ptr) & ~1u,
                             apc_context, io_status_block, 0);
        }
      }

      if (!file->is_synchronous() && result != X_STATUS_END_OF_FILE) {
        result = X_STATUS_PENDING;
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // TODO(benvanik): async.

      // X_STATUS_PENDING if not returning immediately.
      // XFile is waitable and signalled after each async req completes.
      // reset the input event (->Reset())
      /*xeNtReadFileState* call_state = new xeNtReadFileState();
      XAsyncRequest* request = new XAsyncRequest(
      state, file,
      (XAsyncRequest::CompletionCallback)xeNtReadFileCompleted,
      call_state);*/
      // result = file->Read(buffer.guest_address(), buffer_length, byte_offset,
      //                     request);
      if (io_status_block) {
        io_status_block->status = X_STATUS_PENDING;
        io_status_block->information = 0;
      }

      result = X_STATUS_PENDING;
    }
  }

  if (XFAILED(result) && io_status_block) {
    io_status_block->status = result;
    io_status_block->information = 0;
  }

  if (ev && signal_event) {
    ev->Set(0, false);
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT2(NtReadFile, kFileSystem, kImplemented, kHighFrequency);

dword_result_t NtReadFileScatter_entry(
    dword_t file_handle, dword_t event_handle, lpvoid_t apc_routine_ptr,
    lpvoid_t apc_context, pointer_t<X_IO_STATUS_BLOCK> io_status_block,
    lpdword_t segment_array, dword_t length, lpqword_t byte_offset_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  bool signal_event = false;
  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
  if (event_handle && !ev) {
    result = X_STATUS_INVALID_HANDLE;
  }

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (XSUCCEEDED(result)) {
    if (true || file->is_synchronous()) {
      // Synchronous.
      uint32_t bytes_read = 0;
      result = file->ReadScatter(
          segment_array.guest_address(), length,
          byte_offset_ptr ? static_cast<uint64_t>(*byte_offset_ptr) : -1,
          &bytes_read, apc_context);
      if (io_status_block) {
        io_status_block->status = result;
        io_status_block->information = bytes_read;
      }

      // Queue the APC callback. It must be delivered via the APC mechanism even
      // though were are completing immediately.
      // Low bit probably means do not queue to IO ports.
      if ((uint32_t)apc_routine_ptr & ~1) {
        if (apc_context) {
          auto thread = XThread::GetCurrentThread();
          thread->EnqueueApc(static_cast<uint32_t>(apc_routine_ptr) & ~1u,
                             apc_context, io_status_block, 0);
        }
      }

      if (!file->is_synchronous()) {
        result = X_STATUS_PENDING;
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // TODO(benvanik): async.

      // TODO: On Windows it might be worth trying to use Win32 ReadFileScatter
      // here instead of handling it ourselves

      // X_STATUS_PENDING if not returning immediately.
      // XFile is waitable and signalled after each async req completes.
      // reset the input event (->Reset())
      /*xeNtReadFileState* call_state = new xeNtReadFileState();
      XAsyncRequest* request = new XAsyncRequest(
      state, file,
      (XAsyncRequest::CompletionCallback)xeNtReadFileCompleted,
      call_state);*/
      // result = file->Read(buffer.guest_address(), buffer_length, byte_offset,
      //                     request);
      if (io_status_block) {
        io_status_block->status = X_STATUS_PENDING;
        io_status_block->information = 0;
      }

      result = X_STATUS_PENDING;
    }
  }

  if (XFAILED(result) && io_status_block) {
    io_status_block->status = result;
    io_status_block->information = 0;
  }

  if (ev && signal_event) {
    ev->Set(0, false);
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtReadFileScatter, kFileSystem, kImplemented);

dword_result_t NtWriteFile_entry(dword_t file_handle, dword_t event_handle,
                                 function_t apc_routine, lpvoid_t apc_context,
                                 pointer_t<X_IO_STATUS_BLOCK> io_status_block,
                                 lpvoid_t buffer, dword_t buffer_length,
                                 lpqword_t byte_offset_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  // Grab event to signal.
  bool signal_event = false;
  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
  if (event_handle && !ev) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Grab file.
  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Execute write.
  if (XSUCCEEDED(result)) {
    // TODO(benvanik): async path.
    if (true || file->is_synchronous()) {
      // Synchronous request.
      uint32_t bytes_written = 0;
      result = file->Write(
          buffer.guest_address(), buffer_length,
          byte_offset_ptr ? static_cast<uint64_t>(*byte_offset_ptr) : -1,
          &bytes_written, apc_context);

      if (io_status_block) {
        io_status_block->status = result;
        io_status_block->information = static_cast<uint32_t>(bytes_written);
      }

      // Queue the APC callback. It must be delivered via the APC mechanism even
      // though were are completing immediately.
      // Low bit probably means do not queue to IO ports.
      if ((uint32_t)apc_routine & ~1) {
        if (apc_context) {
          auto thread = XThread::GetCurrentThread();
          thread->EnqueueApc(static_cast<uint32_t>(apc_routine) & ~1u,
                             apc_context, io_status_block, 0);
        }
      }

      if (!file->is_synchronous()) {
        result = X_STATUS_PENDING;
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // X_STATUS_PENDING if not returning immediately.
      result = X_STATUS_PENDING;

      if (io_status_block) {
        io_status_block->status = X_STATUS_PENDING;
        io_status_block->information = 0;
      }
    }
  }

  if (XFAILED(result) && io_status_block) {
    io_status_block->status = result;
    io_status_block->information = 0;
  }

  if (ev && signal_event) {
    ev->Set(0, false);
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtWriteFile, kFileSystem, kImplemented);

dword_result_t NtCreateIoCompletion_entry(lpdword_t out_handle,
                                          dword_t desired_access,
                                          lpvoid_t object_attribs,
                                          dword_t num_concurrent_threads) {
  auto completion = new XIOCompletion(kernel_state());
  if (out_handle) {
    *out_handle = completion->handle();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtCreateIoCompletion, kFileSystem, kImplemented);

dword_result_t NtSetIoCompletion_entry(dword_t handle, dword_t key_context,
                                       dword_t apc_context,
                                       dword_t completion_status,
                                       dword_t num_bytes) {
  auto port =
      kernel_state()->object_table()->LookupObject<XIOCompletion>(handle);
  if (!port) {
    return X_STATUS_INVALID_HANDLE;
  }

  XIOCompletion::IONotification notification;
  notification.key_context = key_context;
  notification.apc_context = apc_context;
  notification.num_bytes = num_bytes;
  notification.status = completion_status;

  port->QueueNotification(notification);
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(NtSetIoCompletion, kFileSystem, kImplemented,
                         kHighFrequency);

// Dequeues a packet from the completion port.
dword_result_t NtRemoveIoCompletion_entry(
    dword_t handle, lpdword_t key_context, lpdword_t apc_context,
    pointer_t<X_IO_STATUS_BLOCK> io_status_block, lpqword_t timeout) {
  X_STATUS status = X_STATUS_SUCCESS;
  uint32_t info = 0;

  auto port =
      kernel_state()->object_table()->LookupObject<XIOCompletion>(handle);
  if (!port) {
    status = X_STATUS_INVALID_HANDLE;
  }

  uint64_t timeout_ticks =
      timeout ? static_cast<uint32_t>(*timeout)
              : static_cast<uint64_t>(std::numeric_limits<int64_t>::min());
  XIOCompletion::IONotification notification;
  if (port->WaitForNotification(timeout_ticks, &notification)) {
    if (key_context) {
      *key_context = notification.key_context;
    }
    if (apc_context) {
      *apc_context = notification.apc_context;
    }

    if (io_status_block) {
      io_status_block->status = notification.status;
      io_status_block->information = notification.num_bytes;
    }
  } else {
    status = X_STATUS_TIMEOUT;
  }

  return status;
}
DECLARE_XBOXKRNL_EXPORT2(NtRemoveIoCompletion, kFileSystem, kImplemented,
                         kHighFrequency);

dword_result_t NtQueryFullAttributesFile_entry(
    pointer_t<X_OBJECT_ATTRIBUTES> obj_attribs,
    pointer_t<X_FILE_NETWORK_OPEN_INFORMATION> file_info) {
  auto object_name =
      kernel_memory()->TranslateVirtual<X_ANSI_STRING*>(obj_attribs->name_ptr);

  object_ref<XFile> root_file;
  if (obj_attribs->root_directory != 0xFFFFFFFD &&  // ObDosDevices
      obj_attribs->root_directory != 0) {
    root_file = kernel_state()->object_table()->LookupObject<XFile>(
        obj_attribs->root_directory);
    assert_not_null(root_file);
    assert_true(root_file->type() == XObject::Type::File);
    assert_always();
  }

  auto target_path = util::TranslateAnsiPath(kernel_memory(), object_name);

  // Enforce that the path is ASCII.
  if (!IsValidPath(target_path, false)) {
    return X_STATUS_OBJECT_NAME_INVALID;
  }

  // Resolve the file using the virtual file system.
  auto entry = kernel_state()->file_system()->ResolvePath(target_path);
  if (entry) {
    // Found.
    file_info->creation_time = entry->create_timestamp();
    file_info->last_access_time = entry->access_timestamp();
    file_info->last_write_time = entry->write_timestamp();
    file_info->change_time = entry->write_timestamp();
    file_info->allocation_size = entry->allocation_size();
    file_info->end_of_file = entry->size();
    file_info->attributes = entry->attributes();

    return X_STATUS_SUCCESS;
  }

  return X_STATUS_NO_SUCH_FILE;
}
DECLARE_XBOXKRNL_EXPORT1(NtQueryFullAttributesFile, kFileSystem, kImplemented);

dword_result_t NtQueryDirectoryFile_entry(
    dword_t file_handle, dword_t event_handle, function_t apc_routine,
    lpvoid_t apc_context, pointer_t<X_IO_STATUS_BLOCK> io_status_block,
    pointer_t<X_FILE_DIRECTORY_INFORMATION> file_info_ptr, dword_t length,
    pointer_t<X_ANSI_STRING> file_name, dword_t restart_scan) {
  if (length < 72) {
    return X_STATUS_INFO_LENGTH_MISMATCH;
  }

  X_STATUS result = X_STATUS_UNSUCCESSFUL;
  uint32_t info = 0;

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  auto name = util::TranslateAnsiPath(kernel_memory(), file_name);

  // Enforce that the path is ASCII.
  if (!IsValidPath(name, true)) {
    return X_STATUS_INVALID_PARAMETER;
  }

  if (file) {
    X_FILE_DIRECTORY_INFORMATION dir_info = {0};
    result =
        file->QueryDirectory(file_info_ptr, length, name, restart_scan != 0);
    if (XSUCCEEDED(result)) {
      info = length;
    }
  } else {
    result = X_STATUS_NO_SUCH_FILE;
  }

  if (XFAILED(result)) {
    info = 0;
  }

  if (io_status_block) {
    io_status_block->status = result;
    io_status_block->information = info;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtQueryDirectoryFile, kFileSystem, kImplemented);

dword_result_t NtFlushBuffersFile_entry(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr) {
  auto result = X_STATUS_SUCCESS;

  if (io_status_block_ptr) {
    io_status_block_ptr->status = result;
    io_status_block_ptr->information = 0;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(NtFlushBuffersFile, kFileSystem, kStub);

// https://docs.microsoft.com/en-us/windows/win32/devnotes/ntopensymboliclinkobject
dword_result_t NtOpenSymbolicLinkObject_entry(
    lpdword_t handle_out, pointer_t<X_OBJECT_ATTRIBUTES> object_attrs) {
  if (!object_attrs) {
    return X_STATUS_INVALID_PARAMETER;
  }
  assert_not_null(handle_out);

  assert_true(object_attrs->attributes == 64);  // case insensitive

  auto object_name =
      kernel_memory()->TranslateVirtual<X_ANSI_STRING*>(object_attrs->name_ptr);

  auto target_path = util::TranslateAnsiPath(kernel_memory(), object_name);

  // Enforce that the path is ASCII.
  if (!IsValidPath(target_path, false)) {
    return X_STATUS_OBJECT_NAME_INVALID;
  }

  if (object_attrs->root_directory != 0) {
    assert_always();
  }

  if (utf8::starts_with(target_path, "\\??\\")) {
    target_path = target_path.substr(4);  // Strip the full qualifier
  }

  std::string link_path;
  if (!kernel_state()->file_system()->FindSymbolicLink(target_path,
                                                       link_path)) {
    return X_STATUS_NO_SUCH_FILE;
  }

  object_ref<XSymbolicLink> symlink(new XSymbolicLink(kernel_state()));
  symlink->Initialize(target_path, link_path);

  *handle_out = symlink->handle();

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtOpenSymbolicLinkObject, kFileSystem, kImplemented);

// https://docs.microsoft.com/en-us/windows/win32/devnotes/ntquerysymboliclinkobject
dword_result_t NtQuerySymbolicLinkObject_entry(
    dword_t handle, pointer_t<X_ANSI_STRING> target) {
  auto symlink =
      kernel_state()->object_table()->LookupObject<XSymbolicLink>(handle);
  if (!symlink) {
    return X_STATUS_NO_SUCH_FILE;
  }
  auto length = std::min(static_cast<size_t>(target->maximum_length),
                         symlink->target().size());
  if (length > 0) {
    auto target_buf = kernel_memory()->TranslateVirtual(target->pointer);
    std::memcpy(target_buf, symlink->target().c_str(), length);
  }
  target->length = static_cast<uint16_t>(length);
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtQuerySymbolicLinkObject, kFileSystem, kImplemented);

dword_result_t FscGetCacheElementCount_entry(dword_t r3) { return 0; }
DECLARE_XBOXKRNL_EXPORT1(FscGetCacheElementCount, kFileSystem, kStub);

dword_result_t FscSetCacheElementCount_entry(dword_t unk_0, dword_t unk_1) {
  // unk_0 = 0
  // unk_1 looks like a count? in what units? 256 is a common value
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(FscSetCacheElementCount, kFileSystem, kStub);
// todo: this should fill in the io status block and queue the apc
dword_result_t NtDeviceIoControlFile_entry(
    dword_t handle, dword_t event_handle, dword_t apc_routine,
    dword_t apc_context, pointer_t<X_IO_STATUS_BLOCK> io_status_block,
    dword_t io_control_code, lpvoid_t input_buffer, dword_t input_buffer_len,
    lpvoid_t output_buffer, dword_t output_buffer_len) {
  // Called by XMountUtilityDrive cache-mounting code
  // (checks if the returned values look valid, values below seem to pass the
  // checks)
  const uint32_t cache_size = 0xFF000;

  if (io_control_code == X_IOCTL_DISK_GET_DRIVE_GEOMETRY) {
    if (output_buffer_len < 0x8) {
      assert_always();
      return X_STATUS_BUFFER_TOO_SMALL;
    }
    xe::store_and_swap<uint32_t>(output_buffer, cache_size / 512);
    xe::store_and_swap<uint32_t>(output_buffer + 4, 512);
  } else if (io_control_code == X_IOCTL_DISK_GET_PARTITION_INFO) {
    if (output_buffer_len < 0x10) {
      assert_always();
      return X_STATUS_BUFFER_TOO_SMALL;
    }
    xe::store_and_swap<uint64_t>(output_buffer, 0);
    xe::store_and_swap<uint64_t>(output_buffer + 8, cache_size);
  } else {
    XELOGD("NtDeviceIoControlFile(0x{:X}) - unhandled IOCTL!",
           uint32_t(io_control_code));
    assert_always();
    return X_STATUS_INVALID_PARAMETER;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtDeviceIoControlFile, kFileSystem, kStub);
// device_extension_size = additional bytes of data (aligned up to 8 byte
// granularity) that will be allocated at the tail of the resulting device
// object. although it is allocated at the tail, it is accessed through a
// pointer at offset 0x18 so in theory a guest could be unaware that its a
// single allocation device_name is optional, extra_device_object_attributes
// gets assigned to the attributes field of the OBJECT_ATTRIBUTES used for
// ObCreateObject

// todo: need device guest object struct + host object for device
dword_result_t IoCreateDevice_entry(dword_t driver_object,
                                    dword_t device_extension_size,
                                    pointer_t<X_ANSI_STRING> device_name,
                                    dword_t device_type,
                                    dword_t extra_device_object_attributes,
                                    lpdword_t device_object,
                                    const ppc_context_t& ctx) {
  // Called from XMountUtilityDrive XAM-task code
  // That code tries writing things to a pointer at out_struct+0x18
  // We'll alloc some scratch space for it so it doesn't cause any exceptions

  // 0x24 is guessed size from accesses to out_struct - likely incorrect
  auto current_kernel = ctx->kernel_state;

  uint32_t required_size = 80 + xe::align<uint32_t>(device_extension_size, 8);

  auto kernel_mem = current_kernel->memory();

  auto out_guest = kernel_mem->SystemHeapAlloc(required_size);

  auto out = kernel_mem->TranslateVirtual<uint8_t*>(out_guest);

  memset(out, 0, required_size);

  xe::store<unsigned char>(out, 3);  // maybe device object's Ob type?

  // this stores the total object size, without alignment!

  xe::store_and_swap<uint16_t>(out + 2, device_extension_size + 80);

  // from 17559
  if (device_type == 7 || device_type == 58 || device_type == 62 ||
      device_type == 45 || device_type == 2 || device_type == 60 ||
      device_type == 61 || device_type == 36 || device_type == 64 ||
      device_type == 65 || device_type == 66 || device_type == 67 ||
      device_type == 68 || device_type == 69 || device_type == 70 ||
      device_type == 72 || device_type == 73) {
    xe::store_and_swap<uint32_t>(out + 0xC, 0);
  } else {
    // pointer to itself?
    xe::store_and_swap<uint32_t>(out + 0xC, out_guest);
  }
  xe::store<uint8_t>(out + 0x1C, static_cast<uint8_t>(device_type));

  uint32_t flags_field_value = 16;
  if (device_name) {
    flags_field_value |= 8;
  }
  xe::store<unsigned char>(out + 0x1e, 1);
  xe::store_and_swap<uint32_t>(out + 0x14, flags_field_value);
  if (device_extension_size != 0) {
    // pointer to device specific data
    //  XMountUtilityDrive writes some kind of header here
    xe::store_and_swap<uint32_t>(out + 0x18, out_guest + 80);
  }

  xe::store_and_swap<uint32_t>(out + 8, driver_object);

  *device_object = out_guest;
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(IoCreateDevice, kFileSystem, kStub);

// supposed to invoke a callback on the driver object! its some sort of
// destructor function intended to be called for all devices created from the
// driver
void IoDeleteDevice_entry(dword_t device_ptr, const ppc_context_t& ctx) {
  if (device_ptr) {
    auto kernel_mem = ctx->kernel_state->memory();
    kernel_mem->SystemHeapFree(device_ptr);
  }
}

DECLARE_XBOXKRNL_EXPORT1(IoDeleteDevice, kFileSystem, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Io);
