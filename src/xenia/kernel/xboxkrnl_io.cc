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
#include "xenia/kernel/async_request.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/fs/device.h"
#include "xenia/kernel/objects/xevent.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

using namespace xe::kernel::fs;

class X_OBJECT_ATTRIBUTES {
 public:
  uint32_t root_directory;
  uint32_t object_name_ptr;
  X_ANSI_STRING object_name;
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
static_assert_size(X_OBJECT_ATTRIBUTES, 12 + sizeof(X_ANSI_STRING));

struct FileDisposition {
  static const uint32_t X_FILE_SUPERSEDE = 0x00000000;
  static const uint32_t X_FILE_OPEN = 0x00000001;
  static const uint32_t X_FILE_CREATE = 0x00000002;
  static const uint32_t X_FILE_OPEN_IF = 0x00000003;
  static const uint32_t X_FILE_OVERWRITE = 0x00000004;
  static const uint32_t X_FILE_OVERWRITE_IF = 0x00000005;
};

struct FileAccess {
  static const uint32_t X_GENERIC_READ = 0x80000000;
  static const uint32_t X_GENERIC_WRITE = 0x40000000;
  static const uint32_t X_GENERIC_EXECUTE = 0x20000000;
  static const uint32_t X_GENERIC_ALL = 0x10000000;
  static const uint32_t X_FILE_READ_DATA = 0x00000001;
  static const uint32_t X_FILE_WRITE_DATA = 0x00000002;
  static const uint32_t X_FILE_APPEND_DATA = 0x00000004;
};

X_STATUS NtCreateFile(PPCContext* ppc_state, KernelState* state,
                      uint32_t handle_ptr, uint32_t desired_access,
                      X_OBJECT_ATTRIBUTES* object_attrs,
                      const char* object_name, uint32_t io_status_block_ptr,
                      uint32_t allocation_size_ptr, uint32_t file_attributes,
                      uint32_t share_access, uint32_t creation_disposition) {
  uint64_t allocation_size = 0;  // is this correct???
  if (allocation_size_ptr != 0) {
    allocation_size = SHIM_MEM_64(allocation_size_ptr);
  }

  X_STATUS result = X_STATUS_NO_SUCH_FILE;
  uint32_t info = X_FILE_DOES_NOT_EXIST;
  uint32_t handle;

  FileSystem* fs = state->file_system();
  std::unique_ptr<Entry> entry;

  XFile* root_file = NULL;
  if (object_attrs->root_directory != 0xFFFFFFFD &&  // ObDosDevices
      object_attrs->root_directory != 0) {
    result = state->object_table()->GetObject(object_attrs->root_directory,
                                              (XObject**)&root_file);
    assert_true(XSUCCEEDED(result));
    assert_true(root_file->type() == XObject::Type::kTypeFile);

    // Resolve the file using the device the root directory is part of.
    auto device = root_file->device();
    auto target_path = xe::join_paths(root_file->path(), object_name);
    entry = device->ResolvePath(target_path.c_str());
  } else {
    // Resolve the file using the virtual file system.
    entry = fs->ResolvePath(object_name);
  }

  bool wants_write = desired_access & FileAccess::X_GENERIC_WRITE ||
                     desired_access & FileAccess::X_GENERIC_ALL ||
                     desired_access & FileAccess::X_FILE_WRITE_DATA ||
                     desired_access & FileAccess::X_FILE_APPEND_DATA;
  if (wants_write) {
    if (entry && entry->is_read_only()) {
      // We don't support any write modes.
      XELOGW("Attempted to open the file/dir for create/write");
      desired_access = FileAccess::X_GENERIC_READ;
    }
  }

  XFile* file = nullptr;
  if (!entry) {
    result = X_STATUS_NO_SUCH_FILE;
    info = X_FILE_DOES_NOT_EXIST;
  } else {
    // Open the file/directory.
    fs::Mode mode;
    if (desired_access & FileAccess::X_FILE_APPEND_DATA) {
      mode = fs::Mode::READ_APPEND;
    } else if (wants_write) {
      mode = fs::Mode::READ_WRITE;
    } else {
      mode = fs::Mode::READ;
    }
    result = fs->Open(std::move(entry), state, mode,
                      false,  // TODO(benvanik): pick async mode, if needed.
                      &file);
  }

  if (XSUCCEEDED(result)) {
    // Handle ref is incremented, so return that.
    handle = file->handle();
    file->Release();
    result = X_STATUS_SUCCESS;
    info = X_FILE_OPENED;
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

SHIM_CALL NtCreateFile_shim(PPCContext* ppc_state, KernelState* state) {
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

  auto result =
      NtCreateFile(ppc_state, state, handle_ptr, desired_access, &object_attrs,
                   object_name, io_status_block_ptr, allocation_size_ptr,
                   file_attributes, share_access, creation_disposition);

  free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtOpenFile_shim(PPCContext* ppc_state, KernelState* state) {
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

  auto result = NtCreateFile(ppc_state, state, handle_ptr, desired_access,
                             &object_attrs, object_name, io_status_block_ptr, 0,
                             0, 0, FileDisposition::X_FILE_OPEN);

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

SHIM_CALL NtReadFile_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine_ptr = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t buffer = SHIM_GET_ARG_32(5);
  uint32_t buffer_length = SHIM_GET_ARG_32(6);
  uint32_t byte_offset_ptr = SHIM_GET_ARG_32(7);
  size_t byte_offset = byte_offset_ptr ? SHIM_MEM_64(byte_offset_ptr) : 0;

  XELOGD("NtReadFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%d))",
         file_handle, event_handle, apc_routine_ptr, apc_context,
         io_status_block_ptr, buffer, buffer_length, byte_offset_ptr,
         byte_offset);

  // Async not supported yet.
  assert_zero(apc_routine_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab event to signal.
  XEvent* ev = NULL;
  bool signal_event = false;
  if (event_handle) {
    result = state->object_table()->GetObject(event_handle, (XObject**)&ev);
  }

  // Grab file.
  XFile* file = NULL;
  if (XSUCCEEDED(result)) {
    result = state->object_table()->GetObject(file_handle, (XObject**)&file);
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

  if (file) {
    file->Release();
  }
  if (ev) {
    if (signal_event) {
      ev->Set(0, false);
    }
    ev->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtWriteFile_shim(PPCContext* ppc_state, KernelState* state) {
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
  XEvent* ev = NULL;
  bool signal_event = false;
  if (event_handle) {
    result = state->object_table()->GetObject(event_handle, (XObject**)&ev);
  }

  // Grab file.
  XFile* file = NULL;
  if (XSUCCEEDED(result)) {
    result = state->object_table()->GetObject(file_handle, (XObject**)&file);
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

  if (file) {
    file->Release();
  }
  if (ev) {
    if (signal_event) {
      ev->Set(0, false);
    }
    ev->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtSetInformationFile_shim(PPCContext* ppc_state, KernelState* state) {
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
  XFile* file = NULL;
  result = state->object_table()->GetObject(file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
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
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryInformationFile_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t file_info_class = SHIM_GET_ARG_32(4);

  XELOGD("NtQueryInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)", file_handle,
         io_status_block_ptr, file_info_ptr, length, file_info_class);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  XFile* file = NULL;
  result = state->object_table()->GetObject(file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
    switch (file_info_class) {
      case XFileInternalInformation:
        // Internal unique file pointer. Not sure why anyone would want this.
        assert_true(length == 8);
        info = 8;
        // TODO(benvanik): use pointer to fs:: entry?
        SHIM_SET_MEM_64(file_info_ptr,
                        xe::hash_combine(0, file->path()));
        break;
      case XFilePositionInformation:
        // struct FILE_POSITION_INFORMATION {
        //   LARGE_INTEGER CurrentByteOffset;
        // };
        assert_true(length == 8);
        info = 8;
        SHIM_SET_MEM_64(file_info_ptr, file->position());
        break;
      case XFileNetworkOpenInformation:
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
        X_FILE_NETWORK_OPEN_INFORMATION file_info;
        result = file->QueryInfo(&file_info);
        if (XSUCCEEDED(result)) {
          info = 56;
          file_info.Write(SHIM_MEM_BASE, file_info_ptr);
        }
        break;
      case XFileXctdCompressionInformation:
        // Read timeout.
        if (length == 4) {
          uint32_t magic;
          size_t bytes_read;
          result = file->Read(&magic, sizeof(magic), 0, &bytes_read);
          if (XSUCCEEDED(result)) {
            if (bytes_read == sizeof(magic)) {
              info = 4;
              SHIM_SET_MEM_32(file_info_ptr,
                              magic == xe::byte_swap(0x0FF512ED));
            } else {
              result = X_STATUS_UNSUCCESSFUL;
            }
          }
        } else {
          result = X_STATUS_INFO_LENGTH_MISMATCH;
        }
        break;
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryFullAttributesFile_shim(PPCContext* ppc_state,
                                         KernelState* state) {
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(0);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(1);

  X_OBJECT_ATTRIBUTES attrs(SHIM_MEM_BASE, object_attributes_ptr);

  char* object_name = attrs.object_name.Duplicate();

  XELOGD("NtQueryFullAttributesFile(%.8X(%s), %.8X)", object_attributes_ptr,
         !object_name ? "(null)" : object_name, file_info_ptr);

  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  XFile* root_file = NULL;
  if (attrs.root_directory != 0xFFFFFFFD) {  // ObDosDevices
    result = state->object_table()->GetObject(attrs.root_directory,
                                              (XObject**)&root_file);
    assert_true(XSUCCEEDED(result));
    assert_true(root_file->type() == XObject::Type::kTypeFile);
    assert_always();
  }

  // Resolve the file using the virtual file system.
  FileSystem* fs = state->file_system();
  auto entry = fs->ResolvePath(object_name);
  if (entry) {
    // Found.
    X_FILE_NETWORK_OPEN_INFORMATION file_info;
    result = entry->QueryInfo(&file_info);
    if (XSUCCEEDED(result)) {
      file_info.Write(SHIM_MEM_BASE, file_info_ptr);
    }
  }

  free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryVolumeInformationFile_shim(PPCContext* ppc_state,
                                            KernelState* state) {
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
  XFile* file = NULL;
  result = state->object_table()->GetObject(file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
    switch (fs_info_class) {
      case 1: {  // FileFsVolumeInformation
        auto volume_info = (X_FILE_FS_VOLUME_INFORMATION*)calloc(length, 1);
        result = file->device()->QueryVolumeInfo(volume_info, length);
        if (XSUCCEEDED(result)) {
          volume_info->Write(SHIM_MEM_BASE, fs_info_ptr);
          info = length;
        }
        free(volume_info);
        break;
      }
      case 3: {  // FileFsSizeInformation
        auto fs_attribute_info = (X_FILE_FS_SIZE_INFORMATION*)calloc(length, 1);
        result = file->device()->QuerySizeInfo(fs_attribute_info, length);
        if (XSUCCEEDED(result)) {
          fs_attribute_info->Write(SHIM_MEM_BASE, fs_info_ptr);
          info = length;
        }
        free(fs_attribute_info);
        break;
      }
      case 5: {  // FileFsAttributeInformation
        auto fs_attribute_info = (X_FILE_FS_ATTRIBUTE_INFORMATION*)calloc(length, 1);
        result = file->device()->QueryAttributeInfo(fs_attribute_info, length);
        if (XSUCCEEDED(result)) {
          fs_attribute_info->Write(SHIM_MEM_BASE, fs_info_ptr);
          info = length;
        }
        free(fs_attribute_info);
        break;
      }
      case 2: // FileFsLabelInformation
      case 4: // FileFsDeviceInformation
      case 6: // FileFsControlInformation
      case 7: // FileFsFullSizeInformation
      case 8: // FileFsObjectIdInformation
      default:
        // Unsupported, for now.
        assert_always();
        info = 0;
        break;
    }
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryDirectoryFile_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(5);
  uint32_t length = SHIM_GET_ARG_32(6);
  uint32_t file_name_ptr = SHIM_GET_ARG_32(7);
  uint32_t sp = (uint32_t)ppc_state->r[1];
  uint32_t restart_scan = SHIM_MEM_32(sp + 0x54);

  char* file_name = NULL;
  if (file_name_ptr != 0) {
    X_ANSI_STRING xas(SHIM_MEM_BASE, file_name_ptr);
    file_name = xas.Duplicate();
  }

  XELOGD(
      "NtQueryDirectoryFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%s), "
      "%d)",
      file_handle, event_handle, apc_routine, apc_context, io_status_block_ptr,
      file_info_ptr, length, file_name_ptr, !file_name ? "(null)" : file_name,
      restart_scan);

  if (length < 72) {
    SHIM_SET_RETURN_32(X_STATUS_INFO_LENGTH_MISMATCH);
    free(file_name);
    return;
  }

  X_STATUS result = X_STATUS_UNSUCCESSFUL;
  uint32_t info = 0;

  XFile* file = NULL;
  result = state->object_table()->GetObject(file_handle, (XObject**)&file);
  if (XSUCCEEDED(result)) {
    X_FILE_DIRECTORY_INFORMATION* dir_info = (X_FILE_DIRECTORY_INFORMATION*)calloc(length, 1);
    result =
        file->QueryDirectory(dir_info, length, file_name, restart_scan != 0);
    if (XSUCCEEDED(result)) {
      dir_info->Write(SHIM_MEM_BASE, file_info_ptr);
      info = length;
    }
    free(dir_info);
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);    // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info);  // Information
  }

  if (file) {
    file->Release();
  }

  free(file_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtFlushBuffersFile_shim(PPCContext* ppc_state, KernelState* state) {
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

SHIM_CALL FscSetCacheElementCount_shim(PPCContext* ppc_state,
                                       KernelState* state) {
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
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtOpenFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReadFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtWriteFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryFullAttributesFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryVolumeInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryDirectoryFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtFlushBuffersFile, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", FscSetCacheElementCount, state);
}
