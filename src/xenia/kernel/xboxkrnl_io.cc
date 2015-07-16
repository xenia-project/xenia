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
#include "xenia/kernel/async_request.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/vfs/device.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

using namespace xe::vfs;

// TODO(benvanik): replace X_OBJECT_ATTRIBUTES with new style and remove this.
class X_ANSI_STRING_OLD {
 private:
  uint16_t length;
  uint16_t maximum_length;
  const char* buffer;

 public:
  X_ANSI_STRING_OLD() { Zero(); }
  X_ANSI_STRING_OLD(const uint8_t* base, uint32_t p) { Read(base, p); }
  void Read(const uint8_t* base, uint32_t p) {
    length = xe::load_and_swap<uint16_t>(base + p);
    maximum_length = xe::load_and_swap<uint16_t>(base + p + 2);
    if (maximum_length) {
      buffer = (const char*)(base + xe::load_and_swap<uint32_t>(base + p + 4));
    } else {
      buffer = 0;
    }
  }
  void Zero() {
    length = maximum_length = 0;
    buffer = 0;
  }
  char* Duplicate() {
    if (!buffer || !length) {
      return nullptr;
    }
    auto copy = (char*)calloc(length + 1, sizeof(char));
    std::strncpy(copy, buffer, length);
    return copy;
  }
  std::string to_string() {
    if (!buffer || !length) {
      return "";
    }
    std::string result(buffer, length);
    return result;
  }
};
// static_assert_size(X_ANSI_STRING, 8);

class X_OBJECT_ATTRIBUTES {
 public:
  uint32_t root_directory;
  uint32_t object_name_ptr;
  X_ANSI_STRING_OLD object_name;
  uint32_t attributes;

  X_OBJECT_ATTRIBUTES() { Zero(); }
  X_OBJECT_ATTRIBUTES(const uint8_t* base, uint32_t p) { Read(base, p); }
  void Read(const uint8_t* base, uint32_t p) {
    root_directory = xe::load_and_swap<uint32_t>(base + p);
    object_name_ptr = xe::load_and_swap<uint32_t>(base + p + 4);
    if (object_name_ptr) {
      object_name.Read(base, object_name_ptr);
    } else {
      object_name.Zero();
    }
    attributes = xe::load_and_swap<uint32_t>(base + p + 8);
  }
  void Zero() {
    root_directory = 0;
    object_name_ptr = 0;
    object_name.Zero();
    attributes = 0;
  }
};

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540287.aspx
class X_FILE_FS_VOLUME_INFORMATION {
 public:
  // FILE_FS_VOLUME_INFORMATION
  uint64_t creation_time;
  uint32_t serial_number;
  uint32_t label_length;
  uint32_t supports_objects;
  char label[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint64_t>(dst + 0, this->creation_time);
    xe::store_and_swap<uint32_t>(dst + 8, this->serial_number);
    xe::store_and_swap<uint32_t>(dst + 12, this->label_length);
    xe::store_and_swap<uint32_t>(dst + 16, this->supports_objects);
    memcpy(dst + 20, this->label, this->label_length);
  }
};
static_assert_size(X_FILE_FS_VOLUME_INFORMATION, 24);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540282.aspx
class X_FILE_FS_SIZE_INFORMATION {
 public:
  // FILE_FS_SIZE_INFORMATION
  uint64_t total_allocation_units;
  uint64_t available_allocation_units;
  uint32_t sectors_per_allocation_unit;
  uint32_t bytes_per_sector;

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint64_t>(dst + 0, this->total_allocation_units);
    xe::store_and_swap<uint64_t>(dst + 8, this->available_allocation_units);
    xe::store_and_swap<uint32_t>(dst + 16, this->sectors_per_allocation_unit);
    xe::store_and_swap<uint32_t>(dst + 20, this->bytes_per_sector);
  }
};
static_assert_size(X_FILE_FS_SIZE_INFORMATION, 24);

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff540251(v=vs.85).aspx
class X_FILE_FS_ATTRIBUTE_INFORMATION {
 public:
  // FILE_FS_ATTRIBUTE_INFORMATION
  uint32_t attributes;
  int32_t maximum_component_name_length;
  uint32_t fs_name_length;
  char fs_name[1];

  void Write(uint8_t* base, uint32_t p) {
    uint8_t* dst = base + p;
    xe::store_and_swap<uint32_t>(dst + 0, this->attributes);
    xe::store_and_swap<uint32_t>(dst + 4, this->maximum_component_name_length);
    xe::store_and_swap<uint32_t>(dst + 8, this->fs_name_length);
    memcpy(dst + 12, this->fs_name, this->fs_name_length);
  }
};
static_assert_size(X_FILE_FS_ATTRIBUTE_INFORMATION, 16);

X_STATUS NtCreateFile(PPCContext* ppc_context, KernelState* kernel_state,
                      uint32_t handle_ptr, uint32_t desired_access,
                      X_OBJECT_ATTRIBUTES* object_attrs,
                      const char* object_name, uint32_t io_status_block_ptr,
                      uint32_t allocation_size_ptr, uint32_t file_attributes,
                      uint32_t share_access,
                      FileDisposition creation_disposition) {
  uint64_t allocation_size = 0;  // is this correct???
  if (allocation_size_ptr != 0) {
    allocation_size = SHIM_MEM_64(allocation_size_ptr);
  }

  // Compute path, possibly attrs relative.
  std::string target_path = object_name;
  if (object_attrs->root_directory != 0xFFFFFFFD &&  // ObDosDevices
      object_attrs->root_directory != 0) {
    auto root_file = kernel_state->object_table()->LookupObject<XFile>(
        object_attrs->root_directory);
    assert_not_null(root_file);
    assert_true(root_file->type() == XObject::Type::kTypeFile);

    // Resolve the file using the device the root directory is part of.
    auto device = root_file->device();
    target_path = xe::join_paths(
        device->mount_path(), xe::join_paths(root_file->path(), object_name));
  }

  // Attempt open (or create).
  object_ref<XFile> file;
  FileAction file_action;
  X_STATUS result = kernel_state->file_system()->OpenFile(
      kernel_state, target_path, creation_disposition, desired_access, &file,
      &file_action);
  uint32_t info = uint32_t(file_action);

  X_HANDLE handle = X_INVALID_HANDLE_VALUE;
  if (XSUCCEEDED(result)) {
    // Handle ref is incremented, so return that.
    handle = file->handle();
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }
  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
  }

  return result;
}

SHIM_CALL NtCreateFile_shim(PPCContext* ppc_context,
                            KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t desired_access = SHIM_GET_ARG_32(1);
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(2);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(3);
  uint32_t allocation_size_ptr = SHIM_GET_ARG_32(4);
  uint32_t file_attributes = SHIM_GET_ARG_32(5);
  uint32_t share_access = SHIM_GET_ARG_32(6);
  uint32_t creation_disposition = SHIM_GET_ARG_32(7);

  X_OBJECT_ATTRIBUTES object_attrs(SHIM_MEM_BASE, object_attributes_ptr);
  char* object_name = object_attrs.object_name.Duplicate();

  XELOGD("NtCreateFile(%.8X, %.8X, %.8X(%s), %.8X, %.8X, %.8X, %d, %d)",
         handle_ptr, desired_access, object_attributes_ptr,
         !object_name ? "(null)" : object_name, io_status_block_ptr,
         allocation_size_ptr, file_attributes, share_access,
         creation_disposition);

  auto result = NtCreateFile(
      ppc_context, kernel_state, handle_ptr, desired_access, &object_attrs,
      object_name, io_status_block_ptr, allocation_size_ptr, file_attributes,
      share_access, FileDisposition(creation_disposition));

  free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtOpenFile_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t desired_access = SHIM_GET_ARG_32(1);
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(2);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(3);
  uint32_t open_options = SHIM_GET_ARG_32(4);

  X_OBJECT_ATTRIBUTES object_attrs(SHIM_MEM_BASE, object_attributes_ptr);
  char* object_name = object_attrs.object_name.Duplicate();

  XELOGD("NtOpenFile(%.8X, %.8X, %.8X(%s), %.8X, %d)", handle_ptr,
         desired_access, object_attributes_ptr,
         !object_name ? "(null)" : object_name, io_status_block_ptr,
         open_options);

  auto result = NtCreateFile(
      ppc_context, kernel_state, handle_ptr, desired_access, &object_attrs,
      object_name, io_status_block_ptr, 0, 0, 0, FileDisposition::kOpen);

  free(object_name);
  SHIM_SET_RETURN_32(result);
}

class xeNtReadFileState {
 public:
  uint32_t x;
};
void xeNtReadFileCompleted(XAsyncRequest* request, xeNtReadFileState* state) {
  // TODO(benvanik): set io_status_block_ptr
  delete request;
  delete state;
}

SHIM_CALL NtReadFile_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine_ptr = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t buffer = SHIM_GET_ARG_32(5);
  uint32_t buffer_length = SHIM_GET_ARG_32(6);
  uint32_t byte_offset_ptr = SHIM_GET_ARG_32(7);
  size_t byte_offset = byte_offset_ptr ? SHIM_MEM_64(byte_offset_ptr) : 0;

  XELOGD("NtReadFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%llu))",
         file_handle, event_handle, apc_routine_ptr, apc_context,
         io_status_block_ptr, buffer, buffer_length, byte_offset_ptr,
         byte_offset);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab event to signal.
  bool signal_event = false;
  auto ev =
      event_handle
          ? kernel_state->object_table()->LookupObject<XEvent>(event_handle)
          : object_ref<XEvent>();
  if (event_handle && !ev) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Grab file.
  auto file = kernel_state->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Execute read.
  if (XSUCCEEDED(result)) {
    // Reset event before we begin.
    if (ev) {
      ev->Reset();
    }

    // TODO(benvanik): async path.
    if (true) {
      // Synchronous request.
      if (!byte_offset_ptr || byte_offset == 0xFFFFFFFFfffffffe) {
        // FILE_USE_FILE_POINTER_POSITION
        byte_offset = -1;
      }

      // Read now.
      size_t bytes_read = 0;
      result = file->Read(SHIM_MEM_ADDR(buffer), buffer_length, byte_offset,
                          &bytes_read);
      if (XSUCCEEDED(result)) {
        info = (int32_t)bytes_read;
      }

      // Queue the APC callback. It must be delivered via the APC mechanism even
      // though were are completing immediately.
      if (apc_routine_ptr & ~1) {
        auto thread = XThread::GetCurrentThread();
        thread->EnqueueApc(apc_routine_ptr & ~1, apc_context,
                           io_status_block_ptr, 0);
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // X_STATUS_PENDING if not returning immediately.
      // XFile is waitable and signalled after each async req completes.
      // reset the input event (->Reset())
      /*xeNtReadFileState* call_state = new xeNtReadFileState();
      XAsyncRequest* request = new XAsyncRequest(
          state, file,
          (XAsyncRequest::CompletionCallback)xeNtReadFileCompleted,
          call_state);*/
      // result = file->Read(buffer, buffer_length, byte_offset, request);
      result = X_STATUS_PENDING;
      info = 0;
    }
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (ev && signal_event) {
    ev->Set(0, false);
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtWriteFile_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine_ptr = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t buffer = SHIM_GET_ARG_32(5);
  uint32_t buffer_length = SHIM_GET_ARG_32(6);
  uint32_t byte_offset_ptr = SHIM_GET_ARG_32(7);
  size_t byte_offset = byte_offset_ptr ? SHIM_MEM_64(byte_offset_ptr) : 0;

  XELOGD("NtWriteFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%d))",
         file_handle, event_handle, apc_routine_ptr, apc_context,
         io_status_block_ptr, buffer, buffer_length, byte_offset_ptr,
         byte_offset);

  // Async not supported yet.
  assert_zero(apc_routine_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab event to signal.
  bool signal_event = false;
  auto ev =
      event_handle
          ? kernel_state->object_table()->LookupObject<XEvent>(event_handle)
          : object_ref<XEvent>();
  if (event_handle && !ev) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Grab file.
  auto file = kernel_state->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    result = X_STATUS_INVALID_HANDLE;
  }

  // Execute write.
  if (XSUCCEEDED(result)) {
    // Reset event before we begin.
    if (ev) {
      ev->Reset();
    }

    // TODO(benvanik): async path.
    if (true) {
      // Synchronous request.
      if (!byte_offset_ptr || byte_offset == 0xFFFFFFFFfffffffe) {
        // FILE_USE_FILE_POINTER_POSITION
        byte_offset = -1;
      }

      // Write now.
      size_t bytes_written = 0;
      result = file->Write(SHIM_MEM_ADDR(buffer), buffer_length, byte_offset,
                           &bytes_written);
      if (XSUCCEEDED(result)) {
        info = (int32_t)bytes_written;
      }

      // Mark that we should signal the event now. We do this after
      // we have written the info out.
      signal_event = true;
    } else {
      // X_STATUS_PENDING if not returning immediately.
      result = X_STATUS_PENDING;
      info = 0;
    }
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (ev && signal_event) {
    ev->Set(0, false);
  }

  SHIM_SET_RETURN_32(result);
}

dword_result_t NtCreateIoCompletion(lpvoid_t out_handle, dword_t desired_access,
                                    lpvoid_t object_attribs,
                                    dword_t num_concurrent_threads) {
  return X_STATUS_UNSUCCESSFUL;
}
DECLARE_XBOXKRNL_EXPORT(NtCreateIoCompletion, ExportTag::kStub);

SHIM_CALL NtSetInformationFile_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t file_info_class = SHIM_GET_ARG_32(4);

  XELOGD("NtSetInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)", file_handle,
         io_status_block_ptr, file_info_ptr, length, file_info_class);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  auto file = kernel_state->object_table()->LookupObject<XFile>(file_handle);
  if (file) {
    switch (file_info_class) {
      case XFileDispositionInformation: {
        // Used to set deletion flag. Which we don't support. Probably?
        info = 0;
        bool delete_on_close = SHIM_MEM_8(file_info_ptr) ? true : false;
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
        file->set_position(SHIM_MEM_64(file_info_ptr));
        break;
      case XFileAllocationInformation:
      case XFileEndOfFileInformation:
        assert_true(length == 8);
        info = 8;
        XELOGW("NtSetInformationFile ignoring alloc/eof");
        break;
      case XFileCompletionInformation:
        // Games appear to call NtCreateIoCompletion right before this
        break;
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  SHIM_SET_RETURN_32(result);
}

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
      case XFileXctdCompressionInformation:
        assert_true(length == 4);
        /*
        // This is wrong and puts files into wrong states for games that use
        // XctdDecompression.
        uint32_t magic;
        size_t bytes_read;
        result = file->Read(&magic, sizeof(magic), 0, &bytes_read);
        if (XSUCCEEDED(result)) {
          if (bytes_read == sizeof(magic)) {
            info = 4;
            SHIM_SET_MEM_32(file_info_ptr, magic == xe::byte_swap(0x0FF512ED) ?
        1 : 0);
          } else {
            result = X_STATUS_UNSUCCESSFUL;
          }
        }
        */
        result = X_STATUS_UNSUCCESSFUL;
        break;
      case XFileSectorInformation:
        // TODO: Return sector this file's on.
        assert_true(length == 4);

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

SHIM_CALL NtQueryFullAttributesFile_shim(PPCContext* ppc_context,
                                         KernelState* kernel_state) {
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(0);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(1);

  X_OBJECT_ATTRIBUTES attrs(SHIM_MEM_BASE, object_attributes_ptr);

  char* object_name = attrs.object_name.Duplicate();

  XELOGD("NtQueryFullAttributesFile(%.8X(%s), %.8X)", object_attributes_ptr,
         !object_name ? "(null)" : object_name, file_info_ptr);

  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  object_ref<XFile> root_file;
  if (attrs.root_directory != 0xFFFFFFFD &&  // ObDosDevices
      attrs.root_directory != 0) {
    root_file =
        kernel_state->object_table()->LookupObject<XFile>(attrs.root_directory);
    assert_not_null(root_file);
    assert_true(root_file->type() == XObject::Type::kTypeFile);
    assert_always();
  }

  // Resolve the file using the virtual file system.
  auto fs = kernel_state->file_system();
  auto entry = fs->ResolvePath(object_name);
  if (entry) {
    // Found.
    result = X_STATUS_SUCCESS;

    auto file_info =
        kernel_memory()->TranslateVirtual<X_FILE_NETWORK_OPEN_INFORMATION*>(
            file_info_ptr);

    file_info->creation_time = entry->create_timestamp();
    file_info->last_access_time = entry->access_timestamp();
    file_info->last_write_time = entry->write_timestamp();
    file_info->change_time = entry->write_timestamp();
    file_info->allocation_size = entry->allocation_size();
    file_info->end_of_file = entry->size();
    file_info->attributes = entry->attributes();
  }

  free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryVolumeInformationFile_shim(PPCContext* ppc_context,
                                            KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t fs_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t fs_info_class = SHIM_GET_ARG_32(4);

  XELOGD("NtQueryVolumeInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)",
         file_handle, io_status_block_ptr, fs_info_ptr, length, fs_info_class);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  auto file = kernel_state->object_table()->LookupObject<XFile>(file_handle);
  if (file) {
    switch (fs_info_class) {
      case 1: {  // FileFsVolumeInformation
        // TODO(gibbed): actual value
        std::string name = "test";
        X_FILE_FS_VOLUME_INFORMATION volume_info = {0};
        volume_info.creation_time = 0;
        volume_info.serial_number = 12345678;
        volume_info.supports_objects = 0;
        volume_info.label_length = uint32_t(name.size());
        std::memcpy(volume_info.label, name.data(), name.size());
        volume_info.Write(SHIM_MEM_BASE, fs_info_ptr);
        info = length;
        break;
      }
      case 3: {  // FileFsSizeInformation
        X_FILE_FS_SIZE_INFORMATION fs_size_info = {0};
        fs_size_info.total_allocation_units =
            file->device()->total_allocation_units();
        fs_size_info.available_allocation_units =
            file->device()->available_allocation_units();
        fs_size_info.sectors_per_allocation_unit =
            file->device()->sectors_per_allocation_unit();
        fs_size_info.bytes_per_sector = file->device()->bytes_per_sector();
        fs_size_info.Write(SHIM_MEM_BASE, fs_info_ptr);
        info = length;
        break;
      }
      case 5: {  // FileFsAttributeInformation
        // TODO(gibbed): actual value
        std::string name = "test";
        X_FILE_FS_ATTRIBUTE_INFORMATION fs_attribute_info = {0};
        fs_attribute_info.attributes = 0;
        fs_attribute_info.maximum_component_name_length = 255;
        fs_attribute_info.fs_name_length = uint32_t(name.size());
        std::memcpy(fs_attribute_info.fs_name, name.data(), name.size());
        fs_attribute_info.Write(SHIM_MEM_BASE, fs_info_ptr);
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
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryDirectoryFile_shim(PPCContext* ppc_context,
                                    KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(5);
  uint32_t length = SHIM_GET_ARG_32(6);
  uint32_t file_name_ptr = SHIM_GET_ARG_32(7);
  uint32_t restart_scan = SHIM_GET_ARG_32(8);

  auto file_name = X_ANSI_STRING::to_string(SHIM_MEM_BASE, file_name_ptr);

  XELOGD(
      "NtQueryDirectoryFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%s), "
      "%d)",
      file_handle, event_handle, apc_routine, apc_context, io_status_block_ptr,
      file_info_ptr, length, file_name_ptr,
      !file_name.empty() ? file_name.c_str() : "(null)", restart_scan);

  if (length < 72) {
    SHIM_SET_RETURN_32(X_STATUS_INFO_LENGTH_MISMATCH);
    return;
  }

  X_STATUS result = X_STATUS_UNSUCCESSFUL;
  uint32_t info = 0;

  auto file = kernel_state->object_table()->LookupObject<XFile>(file_handle);
  if (file) {
    X_FILE_DIRECTORY_INFORMATION dir_info = {0};
    result = file->QueryDirectory(
        &dir_info, length, !file_name.empty() ? file_name.c_str() : nullptr,
        restart_scan != 0);
    if (XSUCCEEDED(result)) {
      dir_info.Write(SHIM_MEM_BASE, file_info_ptr);
      info = length;
    }
  } else {
    result = X_STATUS_NO_SUCH_FILE;
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtFlushBuffersFile_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NtFlushBuffersFile(%.8X, %.8X)", file_handle, io_status_block_ptr);

  auto result = X_STATUS_SUCCESS;

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);  // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, 0);   // Information
  }

  SHIM_SET_RETURN_32(result);
}

dword_result_t FscGetCacheElementCount(dword_t r3) { return 0; }
DECLARE_XBOXKRNL_EXPORT(FscGetCacheElementCount, ExportTag::kStub);

SHIM_CALL FscSetCacheElementCount_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  uint32_t unk_0 = SHIM_GET_ARG_32(0);
  uint32_t unk_1 = SHIM_GET_ARG_32(1);
  // unk_0 = 0
  // unk_1 looks like a count? in what units? 256 is a common value

  XELOGD("FscSetCacheElementCount(%.8X, %.8X)", unk_0, unk_1);

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterIoExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtOpenFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReadFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtWriteFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryFullAttributesFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryVolumeInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryDirectoryFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtFlushBuffersFile, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", FscSetCacheElementCount, state);
}
