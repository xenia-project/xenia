/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xiocompletion.h"
#include "xenia/kernel/xthread.h"
#include "xenia/vfs/device.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540287.aspx
struct X_FILE_FS_VOLUME_INFORMATION {
  // FILE_FS_VOLUME_INFORMATION
  xe::be<uint64_t> creation_time;
  xe::be<uint32_t> serial_number;
  xe::be<uint32_t> label_length;
  xe::be<uint32_t> supports_objects;
  char label[1];
};
static_assert_size(X_FILE_FS_VOLUME_INFORMATION, 24);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540282.aspx
struct X_FILE_FS_SIZE_INFORMATION {
  // FILE_FS_SIZE_INFORMATION
  xe::be<uint64_t> total_allocation_units;
  xe::be<uint64_t> available_allocation_units;
  xe::be<uint32_t> sectors_per_allocation_unit;
  xe::be<uint32_t> bytes_per_sector;
};
static_assert_size(X_FILE_FS_SIZE_INFORMATION, 24);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540251(v=vs.85).aspx
struct X_FILE_FS_ATTRIBUTE_INFORMATION {
  // FILE_FS_ATTRIBUTE_INFORMATION
  xe::be<uint32_t> attributes;
  xe::be<int32_t> maximum_component_name_length;
  xe::be<uint32_t> fs_name_length;
  char fs_name[1];
};
static_assert_size(X_FILE_FS_ATTRIBUTE_INFORMATION, 16);

struct CreateOptions {
  // http://processhacker.sourceforge.net/doc/ntioapi_8h.html
  static const uint32_t FILE_DIRECTORY_FILE = 0x00000001;
  // Optimization - files access will be sequential, not random.
  static const uint32_t FILE_SEQUENTIAL_ONLY = 0x00000004;
  static const uint32_t FILE_SYNCHRONOUS_IO_ALERT = 0x00000010;
  static const uint32_t FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020;
  static const uint32_t FILE_NON_DIRECTORY_FILE = 0x00000040;
  // Optimization - file access will be random, not sequential.
  static const uint32_t FILE_RANDOM_ACCESS = 0x00000800;
};

dword_result_t NtCreateFile(lpdword_t handle_out, dword_t desired_access,
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

  // Compute path, possibly attrs relative.
  std::string target_path =
      object_name->to_string(kernel_memory()->virtual_membase());
  if (object_attrs->root_directory != 0xFFFFFFFD &&  // ObDosDevices
      object_attrs->root_directory != 0) {
    auto root_file = kernel_state()->object_table()->LookupObject<XFile>(
        object_attrs->root_directory);
    assert_not_null(root_file);
    assert_true(root_file->type() == XObject::Type::kTypeFile);

    // Resolve the file using the device the root directory is part of.
    auto device = root_file->device();
    target_path = xe::join_paths(
        device->mount_path(), xe::join_paths(root_file->path(), target_path));
  }

  // Attempt open (or create).
  vfs::File* vfs_file;
  vfs::FileAction file_action;
  X_STATUS result = kernel_state()->file_system()->OpenFile(
      target_path, vfs::FileDisposition((uint32_t)creation_disposition),
      desired_access, &vfs_file, &file_action);
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
DECLARE_XBOXKRNL_EXPORT(NtCreateFile, ExportTag::kImplemented);

dword_result_t NtOpenFile(lpdword_t handle_out, dword_t desired_access,
                          pointer_t<X_OBJECT_ATTRIBUTES> object_attributes,
                          pointer_t<X_IO_STATUS_BLOCK> io_status_block,
                          dword_t open_options) {
  return NtCreateFile(handle_out, desired_access, object_attributes,
                      io_status_block, nullptr, 0, 0,
                      static_cast<uint32_t>(xe::vfs::FileDisposition::kOpen),
                      open_options);
}
DECLARE_XBOXKRNL_EXPORT(NtOpenFile, ExportTag::kImplemented);

dword_result_t NtReadFile(dword_t file_handle, dword_t event_handle,
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
      // some games NtReadFile() directly into texture memory
      // TODO(rick): better checking of physical address
      if (buffer.guest_address() >= 0xA0000000) {
        auto heap = kernel_memory()->LookupHeap(buffer.guest_address());
        cpu::MMIOHandler::global_handler()->InvalidateRange(
            heap->GetPhysicalAddress(buffer.guest_address()), buffer_length);
      }

      // Synchronous.
      size_t bytes_read = 0;
      result = file->Read(
          buffer, buffer_length,
          byte_offset_ptr ? static_cast<uint64_t>(*byte_offset_ptr) : -1,
          &bytes_read, apc_context);
      if (io_status_block) {
        io_status_block->status = result;
        io_status_block->information = static_cast<uint32_t>(bytes_read);
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

      // X_STATUS_PENDING if not returning immediately.
      // XFile is waitable and signalled after each async req completes.
      // reset the input event (->Reset())
      /*xeNtReadFileState* call_state = new xeNtReadFileState();
      XAsyncRequest* request = new XAsyncRequest(
      state, file,
      (XAsyncRequest::CompletionCallback)xeNtReadFileCompleted,
      call_state);*/
      // result = file->Read(buffer, buffer_length, byte_offset, request);
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
DECLARE_XBOXKRNL_EXPORT(NtReadFile,
                        ExportTag::kImplemented | ExportTag::kHighFrequency);

dword_result_t NtWriteFile(dword_t file_handle, dword_t event_handle,
                           function_t apc_routine, lpvoid_t apc_context,
                           pointer_t<X_IO_STATUS_BLOCK> io_status_block,
                           lpvoid_t buffer, dword_t buffer_length,
                           lpqword_t byte_offset_ptr) {
  // Async not supported yet.
  assert_zero(apc_routine);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

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
      size_t bytes_written = 0;
      result = file->Write(
          buffer, buffer_length,
          byte_offset_ptr ? static_cast<uint64_t>(*byte_offset_ptr) : -1,
          &bytes_written, apc_context);
      if (XSUCCEEDED(result)) {
        info = (int32_t)bytes_written;
      }

      if (!file->is_synchronous()) {
        result = X_STATUS_PENDING;
      }

      if (io_status_block) {
        io_status_block->status = X_STATUS_SUCCESS;
        io_status_block->information = info;
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // X_STATUS_PENDING if not returning immediately.
      result = X_STATUS_PENDING;
      info = 0;

      if (io_status_block) {
        io_status_block->status = X_STATUS_SUCCESS;
        io_status_block->information = info;
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
DECLARE_XBOXKRNL_EXPORT(NtWriteFile, ExportTag::kImplemented);

dword_result_t NtCreateIoCompletion(lpdword_t out_handle,
                                    dword_t desired_access,
                                    lpvoid_t object_attribs,
                                    dword_t num_concurrent_threads) {
  auto completion = new XIOCompletion(kernel_state());
  if (out_handle) {
    *out_handle = completion->handle();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(NtCreateIoCompletion, ExportTag::kImplemented);

// Dequeues a packet from the completion port.
dword_result_t NtRemoveIoCompletion(
    dword_t handle, lpdword_t key_context, lpdword_t apc_context,
    pointer_t<X_IO_STATUS_BLOCK> io_status_block, lpqword_t timeout) {
  X_STATUS status = X_STATUS_SUCCESS;
  uint32_t info = 0;

  auto port =
      kernel_state()->object_table()->LookupObject<XIOCompletion>(handle);
  if (!port) {
    status = X_STATUS_INVALID_HANDLE;
  }

  uint64_t timeout_ticks = timeout ? static_cast<uint32_t>(*timeout) : 0u;
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
DECLARE_XBOXKRNL_EXPORT(NtRemoveIoCompletion, ExportTag::kImplemented);

dword_result_t NtSetInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block,
    lpvoid_t file_info, dword_t length, dword_t file_info_class) {
  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (XSUCCEEDED(result)) {
    switch (file_info_class) {
      case XFileDispositionInformation: {
        // Used to set deletion flag. Which we don't support. Probably?
        info = 0;
        bool delete_on_close =
            (xe::load_and_swap<uint8_t>(file_info)) ? true : false;
        XELOGW("NtSetInformationFile ignoring delete on close: %d",
               delete_on_close);
        break;
      }
      case XFilePositionInformation:
        // struct FILE_POSITION_INFORMATION {
        //   LARGE_INTEGER CurrentByteOffset;
        // };
        assert_true(length == 8);
        info = 8;
        file->set_position(xe::load_and_swap<uint64_t>(file_info));
        break;
      case XFileAllocationInformation:
      case XFileEndOfFileInformation:
        assert_true(length == 8);
        info = 8;
        XELOGW("NtSetInformationFile ignoring alloc/eof");
        break;
      case XFileCompletionInformation: {
        // Info contains IO Completion handle and completion key
        assert_true(length == 8);

        auto handle = xe::load_and_swap<uint32_t>(file_info + 0x0);
        auto key = xe::load_and_swap<uint32_t>(file_info + 0x4);
        auto port =
            kernel_state()->object_table()->LookupObject<XIOCompletion>(handle);
        if (!port) {
          result = X_STATUS_INVALID_HANDLE;
          break;
        }

        file->RegisterIOCompletionPort(key, port);
        break;
      }
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  }

  if (io_status_block) {
    io_status_block->status = result;
    io_status_block->information = info;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtSetInformationFile,
                        ExportTag::kImplemented | ExportTag::kHighFrequency);

struct X_IO_STATUS_BLOCK {
  union {
    xe::be<uint32_t> status;
    xe::be<uint32_t> pointer;
  };
  xe::be<uint32_t> information;
};

dword_result_t NtQueryInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr,
    lpvoid_t file_info_ptr, dword_t length, dword_t file_info_class) {
  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (file) {
    switch (file_info_class) {
      case XFileInternalInformation:
        // Internal unique file pointer. Not sure why anyone would want this.
        assert_true(length == 8);
        info = 8;
        // TODO(benvanik): use pointer to fs:: entry?
        xe::store_and_swap<uint64_t>(file_info_ptr,
                                     xe::memory::hash_combine(0, file->path()));
        break;
      case XFilePositionInformation:
        // struct FILE_POSITION_INFORMATION {
        //   LARGE_INTEGER CurrentByteOffset;
        // };
        assert_true(length == 8);
        info = 8;
        xe::store_and_swap<uint64_t>(file_info_ptr, file->position());
        break;
      case XFileNetworkOpenInformation: {
        // struct FILE_NETWORK_OPEN_INFORMATION {
        //   LARGE_INTEGER CreationTime;
        //   LARGE_INTEGER LastAccessTime;
        //   LARGE_INTEGER LastWriteTime;
        //   LARGE_INTEGER ChangeTime;
        //   LARGE_INTEGER AllocationSize;
        //   LARGE_INTEGER EndOfFile;
        //   ULONG         FileAttributes;
        //   ULONG         Unknown;
        // };
        assert_true(length == 56);

        auto file_info = file_info_ptr.as<X_FILE_NETWORK_OPEN_INFORMATION*>();
        file_info->creation_time = file->entry()->create_timestamp();
        file_info->last_access_time = file->entry()->access_timestamp();
        file_info->last_write_time = file->entry()->write_timestamp();
        file_info->change_time = file->entry()->write_timestamp();
        file_info->allocation_size = file->entry()->allocation_size();
        file_info->end_of_file = file->entry()->size();
        file_info->attributes = file->entry()->attributes();
        info = 56;
        break;
      }
      case XFileXctdCompressionInformation: {
        assert_true(length == 4);
        XELOGE(
            "NtQueryInformationFile(XFileXctdCompressionInformation) "
            "unimplemented");
        // This is wrong and puts files into wrong states for games that use
        // XctdDecompression.
        /*
        uint32_t magic;
        size_t bytes_read;
        size_t cur_pos = file->position();

        file->set_position(0);
        result = file->Read(&magic, sizeof(magic), 0, &bytes_read);
        if (XSUCCEEDED(result)) {
          if (bytes_read == sizeof(magic)) {
            info = 4;
            *file_info_ptr.as<xe::be<uint32_t>*>() =
                magic == xe::byte_swap(0x0FF512ED) ? 1 : 0;
          } else {
            result = X_STATUS_UNSUCCESSFUL;
          }
        }
        file->set_position(cur_pos);
        info = 4;
        */
        xe::store_and_swap<uint32_t>(file_info_ptr, 0);
        result = X_STATUS_UNSUCCESSFUL;
        info = 0;
      } break;
      case XFileSectorInformation:
        // TODO(benvanik): return sector this file's on.
        assert_true(length == 4);
        XELOGE("NtQueryInformationFile(XFileSectorInformation) unimplemented");
        result = X_STATUS_UNSUCCESSFUL;
        info = 0;
        break;
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        result = X_STATUS_UNSUCCESSFUL;
        break;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (io_status_block_ptr) {
    io_status_block_ptr->status = result;
    io_status_block_ptr->information = info;  // # bytes written
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtQueryInformationFile,
                        ExportTag::kImplemented | ExportTag::kFileSystem);

dword_result_t NtQueryFullAttributesFile(
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
    assert_true(root_file->type() == XObject::Type::kTypeFile);
    assert_always();
  }

  // Resolve the file using the virtual file system.
  auto entry = kernel_state()->file_system()->ResolvePath(
      object_name->to_string(kernel_memory()->virtual_membase()));
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
DECLARE_XBOXKRNL_EXPORT(NtQueryFullAttributesFile, ExportTag::kImplemented);

dword_result_t NtQueryVolumeInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr,
    lpvoid_t fs_info_ptr, dword_t length, dword_t fs_info_class) {
  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (file) {
    switch (fs_info_class) {
      case 1: {  // FileFsVolumeInformation
        // TODO(gibbed): actual value
        std::string name = "test";
        X_FILE_FS_VOLUME_INFORMATION* volume_info =
            fs_info_ptr.as<X_FILE_FS_VOLUME_INFORMATION*>();
        volume_info->creation_time = 0;
        volume_info->serial_number = 12345678;
        volume_info->supports_objects = 0;
        volume_info->label_length = uint32_t(name.size());
        std::memcpy(volume_info->label, name.data(), name.size());
        info = length;
        break;
      }
      case 3: {  // FileFsSizeInformation
        X_FILE_FS_SIZE_INFORMATION* fs_size_info =
            fs_info_ptr.as<X_FILE_FS_SIZE_INFORMATION*>();
        fs_size_info->total_allocation_units =
            file->device()->total_allocation_units();
        fs_size_info->available_allocation_units =
            file->device()->available_allocation_units();
        fs_size_info->sectors_per_allocation_unit =
            file->device()->sectors_per_allocation_unit();
        fs_size_info->bytes_per_sector = file->device()->bytes_per_sector();
        info = length;
        break;
      }
      case 5: {  // FileFsAttributeInformation
        // TODO(gibbed): actual value
        std::string name = "test";
        X_FILE_FS_ATTRIBUTE_INFORMATION* fs_attribute_info =
            fs_info_ptr.as<X_FILE_FS_ATTRIBUTE_INFORMATION*>();
        fs_attribute_info->attributes = 0;
        fs_attribute_info->maximum_component_name_length = 255;
        fs_attribute_info->fs_name_length = uint32_t(name.size());
        std::memcpy(fs_attribute_info->fs_name, name.data(), name.size());
        info = length;
        break;
      }
      case 2:  // FileFsLabelInformation
      case 4:  // FileFsDeviceInformation
      case 6:  // FileFsControlInformation
      case 7:  // FileFsFullSizeInformation
      case 8:  // FileFsObjectIdInformation
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  } else {
    result = X_STATUS_NO_SUCH_FILE;
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    io_status_block_ptr->status = result;
    io_status_block_ptr->information = info;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtQueryVolumeInformationFile, ExportTag::kStub);

dword_result_t NtQueryDirectoryFile(
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
  auto name =
      file_name ? file_name->to_string(kernel_memory()->virtual_membase()) : "";
  if (file) {
    X_FILE_DIRECTORY_INFORMATION dir_info = {0};
    result = file->QueryDirectory(file_info_ptr, length,
                                  !name.empty() ? name.c_str() : nullptr,
                                  restart_scan != 0);
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
DECLARE_XBOXKRNL_EXPORT(NtQueryDirectoryFile, ExportTag::kImplemented);

dword_result_t NtFlushBuffersFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr) {
  auto result = X_STATUS_SUCCESS;

  if (io_status_block_ptr) {
    io_status_block_ptr->status = result;
    io_status_block_ptr->information = 0;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT(NtFlushBuffersFile, ExportTag::kStub);

dword_result_t FscGetCacheElementCount(dword_t r3) { return 0; }
DECLARE_XBOXKRNL_EXPORT(FscGetCacheElementCount, ExportTag::kStub);

dword_result_t FscSetCacheElementCount(dword_t unk_0, dword_t unk_1) {
  // unk_0 = 0
  // unk_1 looks like a count? in what units? 256 is a common value
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(FscSetCacheElementCount, ExportTag::kStub);

void RegisterIoExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
