/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_io.h>

#include <xenia/kernel/async_request.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/objects/xevent.h>
#include <xenia/kernel/objects/xfile.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


namespace xe {
namespace kernel {


SHIM_CALL NtCreateFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t desired_access = SHIM_GET_ARG_32(1);
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(2);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(3);
  uint32_t allocation_size_ptr = SHIM_GET_ARG_32(4);
  uint32_t file_attributes = SHIM_GET_ARG_32(5);
  uint32_t share_access = SHIM_GET_ARG_32(6);
  uint32_t creation_disposition = SHIM_GET_ARG_32(7);

  X_OBJECT_ATTRIBUTES attrs(SHIM_MEM_BASE, object_attributes_ptr);

  char* object_name = attrs.object_name.Duplicate();

  XELOGD(
      "NtCreateFile(%.8X, %.8X, %.8X(%s), %.8X, %.8X, %.8X, %d, %d)",
      handle_ptr,
      desired_access,
      object_attributes_ptr,
      !object_name ? "(null)" : object_name,
      io_status_block_ptr,
      allocation_size_ptr,
      file_attributes,
      share_access,
      creation_disposition);

  uint64_t allocation_size = 0; // is this correct???
  if (allocation_size_ptr != 0) {
    allocation_size = SHIM_MEM_64(allocation_size_ptr);
  }

  X_STATUS result = X_STATUS_NO_SUCH_FILE;
  uint32_t info = X_FILE_DOES_NOT_EXIST;
  uint32_t handle;

  FileSystem* fs = state->file_system();
  Entry* entry;

  XFile* root_file = NULL;
  if (attrs.root_directory != 0xFFFFFFFD && // ObDosDevices
      attrs.root_directory != 0) {
    result = state->object_table()->GetObject(
      attrs.root_directory, (XObject**)&root_file);
    assert_true(XSUCCEEDED(result));
    assert_true(root_file->type() == XObject::Type::kTypeFile);

    auto root_path = root_file->absolute_path();
    auto target_path = xestrdupa((std::string(root_path) + std::string(object_name)).c_str());
    entry = fs->ResolvePath(target_path);
    xe_free(target_path);
  }
  else {
    // Resolve the file using the virtual file system.
    entry = fs->ResolvePath(object_name);
  }

  XFile* file = NULL;
  if (entry && entry->type() == Entry::kTypeFile) {
    // Open the file.
    result = entry->Open(
        state,
        desired_access,
        false, // TODO(benvanik): pick async mode, if needed.
        &file);
  } else {
    result = X_STATUS_NO_SUCH_FILE;
    info = X_FILE_DOES_NOT_EXIST;
  }

  if (XSUCCEEDED(result)) {
    // Handle ref is incremented, so return that.
    handle = file->handle();
    file->Release();
    result  = X_STATUS_SUCCESS;
    info    = X_FILE_OPENED;
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }
  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
  }

  xe_free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtOpenFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t handle_ptr = SHIM_GET_ARG_32(0);
  uint32_t desired_access = SHIM_GET_ARG_32(1);
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(2);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(3);
  uint32_t open_options = SHIM_GET_ARG_32(4);

  X_OBJECT_ATTRIBUTES attrs(SHIM_MEM_BASE, object_attributes_ptr);

  char* object_name = attrs.object_name.Duplicate();

  XELOGD(
    "NtOpenFile(%.8X, %.8X, %.8X(%s), %.8X, %d)",
    handle_ptr,
    desired_access,
    object_attributes_ptr,
    !object_name ? "(null)" : object_name,
    io_status_block_ptr,
    open_options);

  X_STATUS result = X_STATUS_NO_SUCH_FILE;
  uint32_t info = X_FILE_DOES_NOT_EXIST;
  uint32_t handle;

  XFile* root_file = NULL;
  if (attrs.root_directory != 0xFFFFFFFD) { // ObDosDevices
    result = state->object_table()->GetObject(
      attrs.root_directory, (XObject**)&root_file);
    assert_true(XSUCCEEDED(result));
    assert_true(root_file->type() == XObject::Type::kTypeFile);
    assert_always();
  }

  // Resolve the file using the virtual file system.
  FileSystem* fs = state->file_system();
  Entry* entry = fs->ResolvePath(object_name);
  XFile* file = NULL;
  if (entry && entry->type() == Entry::kTypeFile) {
    // Open the file.
    result = entry->Open(
      state,
      desired_access,
      false, // TODO(benvanik): pick async mode, if needed.
      &file);
  }
  else {
    result = X_STATUS_NO_SUCH_FILE;
    info = X_FILE_DOES_NOT_EXIST;
  }

  if (XSUCCEEDED(result)) {
    // Handle ref is incremented, so return that.
    handle = file->handle();
    file->Release();
    result = X_STATUS_SUCCESS;
    info = X_FILE_OPENED;
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }
  if (XSUCCEEDED(result)) {
    if (handle_ptr) {
      SHIM_SET_MEM_32(handle_ptr, handle);
    }
  }

  xe_free(object_name);
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

SHIM_CALL NtReadFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t event_handle = SHIM_GET_ARG_32(1);
  uint32_t apc_routine_ptr = SHIM_GET_ARG_32(2);
  uint32_t apc_context = SHIM_GET_ARG_32(3);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(4);
  uint32_t buffer = SHIM_GET_ARG_32(5);
  uint32_t buffer_length = SHIM_GET_ARG_32(6);
  uint32_t byte_offset_ptr = SHIM_GET_ARG_32(7);
  size_t byte_offset = byte_offset_ptr ? SHIM_MEM_64(byte_offset_ptr) : 0;

  XELOGD(
      "NtReadFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%d))",
      file_handle,
      event_handle,
      apc_routine_ptr,
      apc_context,
      io_status_block_ptr,
      buffer,
      buffer_length,
      byte_offset_ptr,
      byte_offset);

  // Async not supported yet.
  assert_zero(apc_routine_ptr);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab event to signal.
  XEvent* ev = NULL;
  bool signal_event = false;
  if (event_handle) {
    result = state->object_table()->GetObject(
        event_handle, (XObject**)&ev);
  }

  // Grab file.
  XFile* file = NULL;
  if (XSUCCEEDED(result)) {
    result = state->object_table()->GetObject(
        file_handle, (XObject**)&file);
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
      if (!byte_offset_ptr ||
          byte_offset == 0xFFFFFFFFfffffffe) {
        // FILE_USE_FILE_POINTER_POSITION
        byte_offset = -1;
      }

      // Read now.
      size_t bytes_read = 0;
      result = file->Read(
          SHIM_MEM_ADDR(buffer), buffer_length, byte_offset,
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
      //result = file->Read(buffer, buffer_length, byte_offset, request);
      result = X_STATUS_PENDING;
      info = 0;
    }
  }

  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
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

SHIM_CALL NtSetInformationFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t file_info_class = SHIM_GET_ARG_32(4);

  XELOGD(
      "NtSetInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)",
      file_handle,
      io_status_block_ptr,
      file_info_ptr,
      length,
      file_info_class);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  XFile* file = NULL;
  result = state->object_table()->GetObject(
      file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
    switch (file_info_class) {
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
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryInformationFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t file_info_class = SHIM_GET_ARG_32(4);

  XELOGD(
      "NtQueryInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)",
      file_handle,
      io_status_block_ptr,
      file_info_ptr,
      length,
      file_info_class);

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  XFile* file = NULL;
  result = state->object_table()->GetObject(
      file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
    switch (file_info_class) {
    case XFileInternalInformation:
      // Internal unique file pointer. Not sure why anyone would want this.
      assert_true(length == 8);
      info = 8;
      // TODO(benvanik): use pointer to fs:: entry?
      SHIM_SET_MEM_64(file_info_ptr, hash_combine(0, file->absolute_path()));
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
      XFileInfo file_info;
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
            SHIM_SET_MEM_32(file_info_ptr, magic == poly::byte_swap(0x0FF512ED));
          }
          else {
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
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryFullAttributesFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t object_attributes_ptr = SHIM_GET_ARG_32(0);
  uint32_t file_info_ptr = SHIM_GET_ARG_32(1);

  X_OBJECT_ATTRIBUTES attrs(SHIM_MEM_BASE, object_attributes_ptr);

  char* object_name = attrs.object_name.Duplicate();

  XELOGD(
      "NtQueryFullAttributesFile(%.8X(%s), %.8X)",
      object_attributes_ptr,
      !object_name ? "(null)" : object_name,
      file_info_ptr);

  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  XFile* root_file = NULL;
  if (attrs.root_directory != 0xFFFFFFFD) { // ObDosDevices
    result = state->object_table()->GetObject(
      attrs.root_directory, (XObject**)&root_file);
    assert_true(XSUCCEEDED(result));
    assert_true(root_file->type() == XObject::Type::kTypeFile);
    assert_always();
  }

  // Resolve the file using the virtual file system.
  FileSystem* fs = state->file_system();
  Entry* entry = fs->ResolvePath(object_name);
  if (entry && entry->type() == Entry::kTypeFile) {
    // Found.
    XFileInfo file_info;
    result = entry->QueryInfo(&file_info);
    if (XSUCCEEDED(result)) {
      file_info.Write(SHIM_MEM_BASE, file_info_ptr);
    }
  }

  xe_free(object_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryVolumeInformationFile_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t file_handle = SHIM_GET_ARG_32(0);
  uint32_t io_status_block_ptr = SHIM_GET_ARG_32(1);
  uint32_t fs_info_ptr = SHIM_GET_ARG_32(2);
  uint32_t length = SHIM_GET_ARG_32(3);
  uint32_t fs_info_class = SHIM_GET_ARG_32(4);

  XELOGD(
      "NtQueryVolumeInformationFile(%.8X, %.8X, %.8X, %.8X, %.8X)",
      file_handle,
      io_status_block_ptr,
      fs_info_ptr,
      length,
      fs_info_class);


  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t info = 0;

  // Grab file.
  XFile* file = NULL;
  result = state->object_table()->GetObject(
      file_handle, (XObject**)&file);

  if (XSUCCEEDED(result)) {
    result = X_STATUS_SUCCESS;
    switch (fs_info_class) {
    case 1: { // FileFsVolumeInformation
      auto volume_info = (XVolumeInfo*)xe_calloc(length);
      result = file->QueryVolume(volume_info, length);
      if (XSUCCEEDED(result)) {
        volume_info->Write(SHIM_MEM_BASE, fs_info_ptr);
        info = length;
      }
      xe_free(volume_info);
      break;
    }
    case 5: { // FileFsAttributeInformation
      auto fs_attribute_info = (XFileSystemAttributeInfo*)xe_calloc(length);
      result = file->QueryFileSystemAttributes(fs_attribute_info, length);
      if (XSUCCEEDED(result)) {
        fs_attribute_info->Write(SHIM_MEM_BASE, fs_info_ptr);
        info = length;
      }
      xe_free(fs_attribute_info);
      break;
    }
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
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }

  if (file) {
    file->Release();
  }

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL NtQueryDirectoryFile_shim(
    PPCContext* ppc_state, KernelState* state) {
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
    "NtQueryDirectoryFile(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %d, %.8X(%s), %d)",
      file_handle,
      event_handle,
      apc_routine,
      apc_context,
      io_status_block_ptr,
      file_info_ptr,
      length,
      file_name_ptr,
      !file_name ? "(null)" : file_name,
      restart_scan);

  if (length < 72) {
    SHIM_SET_RETURN_32(X_STATUS_INFO_LENGTH_MISMATCH);
    xe_free(file_name);
    return;
  }

  X_STATUS result = X_STATUS_UNSUCCESSFUL;
  uint32_t info = 0;

  XFile* file = NULL;
  result = state->object_table()->GetObject(
      file_handle, (XObject**)&file);
  if (XSUCCEEDED(result)) {
    XDirectoryInfo* dir_info = (XDirectoryInfo*)xe_calloc(length);
    result = file->QueryDirectory(dir_info, length, file_name, restart_scan != 0);
    if (XSUCCEEDED(result)) {
      dir_info->Write(SHIM_MEM_BASE, file_info_ptr);
      info = length;
    }
    xe_free(dir_info);
  }

  if (XFAILED(result)) {
    info = 0;
  }
  if (io_status_block_ptr) {
    SHIM_SET_MEM_32(io_status_block_ptr, result);   // Status
    SHIM_SET_MEM_32(io_status_block_ptr + 4, info); // Information
  }

  if (file) {
    file->Release();
  }

  xe_free(file_name);
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL FscSetCacheElementCount_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk_0 = SHIM_GET_ARG_32(0);
  uint32_t unk_1 = SHIM_GET_ARG_32(1);
  // unk_0 = 0
  // unk_1 looks like a count? in what units? 256 is a common value

  XELOGD(
      "FscSetCacheElementCount(%.8X, %.8X)",
      unk_0, unk_1);

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterIoExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", NtCreateFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtOpenFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtReadFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtSetInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryFullAttributesFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryVolumeInformationFile, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", NtQueryDirectoryFile, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", FscSetCacheElementCount, state);
}
