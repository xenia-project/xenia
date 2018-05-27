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
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

struct DeviceInfo {
  uint32_t device_id;
  uint32_t device_type;
  uint64_t total_bytes;
  uint64_t free_bytes;
  std::wstring name;
};
static const DeviceInfo dummy_device_info_ = {
    0xF00D0000,
    1,
    120ull * 1024ull * 1024ull * 1024ull,  // 120GB
    100ull * 1024ull * 1024ull * 1024ull,  // 100GB, so it looks a little used.
    L"Dummy HDD",
};

dword_result_t XamContentGetLicenseMask(lpdword_t mask_ptr,
                                        lpvoid_t overlapped_ptr) {
  XELOGD("XamContentGetLicenseMask(%.8X, %.8X)", mask_ptr, overlapped_ptr);

  // Each bit in the mask represents a granted license. Available licenses
  // seems to vary from game to game, but most appear to use bit 0 to indicate
  // if the game is purchased or not.
  *mask_ptr = 0x0;

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT(XamContentGetLicenseMask, ExportTag::kImplemented);

dword_result_t XamContentGetDeviceName(dword_t device_id, lpwstring_t name_ptr,
                                       dword_t name_capacity) {
  XELOGD("XamContentGetDeviceName(%.8X, %.8X, %d)", device_id, name_ptr,
         name_capacity);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (name_capacity < dummy_device_info_.name.size() + 1) {
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  xe::store_and_swap<std::wstring>(name_ptr, dummy_device_info_.name);

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamContentGetDeviceName, ExportTag::kImplemented);

dword_result_t XamContentGetDeviceState(dword_t device_id,
                                        lpvoid_t overlapped_ptr) {
  XELOGD("XamContentGetDeviceState(%.8X, %.8X)", device_id, overlapped_ptr);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    if (overlapped_ptr) {
      kernel_state()->CompleteOverlappedImmediateEx(
          overlapped_ptr, X_ERROR_FUNCTION_FAILED, X_ERROR_DEVICE_NOT_CONNECTED,
          0);
      return X_ERROR_IO_PENDING;
    } else {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT(XamContentGetDeviceState, ExportTag::kImplemented);

dword_result_t XamContentGetDeviceData(dword_t device_id,
                                       lpvoid_t device_data_ptr) {
  XELOGD("XamContentGetDeviceData(%.8X, %.8X)", device_id, device_data_ptr);

  if ((device_id & 0xFFFF0000) != dummy_device_info_.device_id) {
    // TODO(benvanik): memset 0 the data?
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  const auto& device_info = dummy_device_info_;
  xe::store_and_swap<uint32_t>(device_data_ptr + 0, device_info.device_id);
  xe::store_and_swap<uint32_t>(device_data_ptr + 4,
                               device_id & 0xFFFF);  // Fake it.
  xe::store_and_swap<uint64_t>(device_data_ptr + 8, device_info.total_bytes);
  xe::store_and_swap<uint64_t>(device_data_ptr + 16, device_info.free_bytes);
  xe::store_and_swap<std::wstring>(device_data_ptr + 24, device_info.name);

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamContentGetDeviceData, ExportTag::kImplemented);

dword_result_t XamContentResolve(dword_t user_index, lpvoid_t content_data_ptr,
                                 lpvoid_t buffer_ptr, dword_t buffer_size,
                                 dword_t unk1 /*1*/, dword_t unk2 /*0*/,
                                 dword_t unk3 /*0*/) {
  auto content_data = XCONTENT_DATA((uint8_t*)content_data_ptr);

  XELOGD("XamContentResolve(%d, %.8X, %.8X, %d, %.8X, %.8X, %.8X)", user_index,
         content_data_ptr, buffer_ptr, buffer_size, unk1, unk2, unk3);

  // Result of buffer_ptr is sent to RtlInitAnsiString.
  // buffer_size is usually 260 (max path).
  // Games expect zero if resolve was successful.
  assert_always();
  XELOGW("XamContentResolve unimplemented!");

  return X_ERROR_NOT_FOUND;
}
DECLARE_XAM_EXPORT(XamContentResolve, ExportTag::kSketchy);

// http://gameservice.googlecode.com/svn-history/r14/trunk/ContentManager.cpp
// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L499
dword_result_t XamContentCreateEnumerator(dword_t user_index, dword_t device_id,
                                          dword_t content_type,
                                          dword_t content_flags,
                                          dword_t max_count,
                                          lpdword_t buffer_size_ptr,
                                          lpdword_t handle_out) {
  assert_not_null(handle_out);
  if ((device_id && (device_id & 0xFFFF0000) != dummy_device_info_.device_id) ||
      !handle_out) {
    if (buffer_size_ptr) {
      *buffer_size_ptr = 0;
    }

    // TODO(benvanik): memset 0 the data?
    return X_E_INVALIDARG;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = (uint32_t)XCONTENT_DATA::kSize * max_count;
  }

  auto e =
      new XStaticEnumerator(kernel_state(), max_count, XCONTENT_DATA::kSize);
  e->Initialize();

  // Get all content data.
  auto content_datas = kernel_state()->content_manager()->ListContent(
      device_id ? static_cast<uint32_t>(device_id)
                : dummy_device_info_.device_id,
      content_type);
  for (auto& content_data : content_datas) {
    auto ptr = e->AppendItem();
    if (!ptr) {
      // Too many items.
      break;
    }

    content_data.Write(ptr);
  }

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT(XamContentCreateEnumerator, ExportTag::kImplemented);

dword_result_t XamContentCreateEx(dword_t user_index, lpstring_t root_name,
                                  lpvoid_t content_data_ptr, dword_t flags,
                                  lpdword_t disposition_ptr,
                                  lpdword_t license_mask_ptr,
                                  dword_t cache_size, qword_t content_size,
                                  lpvoid_t overlapped_ptr) {
  assert_null(license_mask_ptr);

  X_RESULT result = X_ERROR_INVALID_PARAMETER;
  auto content_data = XCONTENT_DATA((uint8_t*)content_data_ptr);

  auto content_manager = kernel_state()->content_manager();
  bool create = false;
  bool open = false;
  switch (flags & 0xF) {
    case 1:  // CREATE_NEW
             // Fail if exists.
      if (content_manager->ContentExists(content_data)) {
        result = X_ERROR_ALREADY_EXISTS;
      } else {
        create = true;
      }
      break;
    case 2:  // CREATE_ALWAYS
             // Overwrite existing, if any.
      if (content_manager->ContentExists(content_data)) {
        content_manager->DeleteContent(content_data);
        create = true;
      } else {
        create = true;
      }
      break;
    case 3:  // OPEN_EXISTING
             // Open only if exists.
      if (!content_manager->ContentExists(content_data)) {
        result = X_ERROR_PATH_NOT_FOUND;
      } else {
        open = true;
      }
      break;
    case 4:  // OPEN_ALWAYS
             // Create if needed.
      if (!content_manager->ContentExists(content_data)) {
        create = true;
      } else {
        open = true;
      }
      break;
    case 5:  // TRUNCATE_EXISTING
             // Fail if doesn't exist, if does exist delete and recreate.
      if (!content_manager->ContentExists(content_data)) {
        result = X_ERROR_PATH_NOT_FOUND;
      } else {
        content_manager->DeleteContent(content_data);
        create = true;
      }
      break;
    default:
      assert_unhandled_case(flags & 0xF);
      break;
  }

  // creation result
  // 0 = ?
  // 1 = created
  // 2 = opened
  uint32_t disposition = create ? 1 : 2;
  if (disposition_ptr) {
    if (overlapped_ptr) {
      // If async always set to zero, but don't set to a real value.
      *disposition_ptr = 0;
    } else {
      *disposition_ptr = disposition;
    }
  }

  if (create) {
    result = content_manager->CreateContent(root_name.value(), content_data);
  } else if (open) {
    result = content_manager->OpenContent(root_name.value(), content_data);
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediateEx(overlapped_ptr, result, 0,
                                                  disposition);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT(XamContentCreateEx, ExportTag::kImplemented);

dword_result_t XamContentCreate(dword_t user_index, lpstring_t root_name,
                                lpvoid_t content_data_ptr, dword_t flags,
                                lpdword_t disposition_ptr,
                                lpdword_t license_mask_ptr,
                                lpvoid_t overlapped_ptr) {
  return XamContentCreateEx(user_index, root_name, content_data_ptr, flags,
                            disposition_ptr, license_mask_ptr, 0, 0,
                            overlapped_ptr);
}
DECLARE_XAM_EXPORT(XamContentCreate, ExportTag::kImplemented);

dword_result_t XamContentOpenFile(dword_t r3, lpstring_t r4, lpstring_t r5,
                                  dword_t r6, dword_t r7, dword_t r8,
                                  dword_t r9) {
  return X_ERROR_FILE_NOT_FOUND;
}
DECLARE_XAM_EXPORT(XamContentOpenFile, ExportTag::kStub);

dword_result_t XamContentFlush(lpstring_t root_name_ptr,
                               lpvoid_t overlapped_ptr) {
  auto root_name = xe::load_and_swap<std::string>(root_name_ptr);

  XELOGD("XamContentFlush(%.8X(%s), %.8X)", root_name_ptr, root_name.c_str(),
         overlapped_ptr);

  X_RESULT result = X_ERROR_SUCCESS;
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}

dword_result_t XamContentClose(lpstring_t root_name_ptr,
                               lpvoid_t overlapped_ptr) {
  auto root_name = xe::load_and_swap<std::string>(root_name_ptr);

  XELOGD("XamContentClose(%.8X(%s), %.8X)", root_name_ptr, root_name.c_str(),
         overlapped_ptr);

  // Closes a previously opened root from XamContentCreate*.
  auto result = kernel_state()->content_manager()->CloseContent(root_name);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT(XamContentClose, ExportTag::kImplemented);

dword_result_t XamContentGetCreator(dword_t user_index,
                                    lpvoid_t content_data_ptr,
                                    lpvoid_t is_creator_ptr,
                                    lpdword_t creator_xuid_ptr,
                                    lpvoid_t overlapped_ptr) {
  auto content_data = XCONTENT_DATA(content_data_ptr);

  XELOGD("XamContentGetCreator(%d, %.8X, %.8X, %.8X, %.8X)", user_index,
         content_data_ptr, is_creator_ptr, creator_xuid_ptr, overlapped_ptr);

  auto result = X_ERROR_SUCCESS;

  if (content_data.content_type == 1) {
    // User always creates saves.
    xe::store_and_swap<uint32_t>(is_creator_ptr, 1);
    if (creator_xuid_ptr) {
      xe::store_and_swap<uint64_t>(creator_xuid_ptr,
                                   kernel_state()->user_profile()->xuid());
    }
  } else {
    xe::store_and_swap<uint32_t>(is_creator_ptr, 0);
    if (creator_xuid_ptr) {
      xe::store_and_swap<uint64_t>(creator_xuid_ptr, 0);
    }
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}

dword_result_t XamContentGetThumbnail(dword_t user_index,
                                      lpvoid_t content_data_ptr,
                                      lpvoid_t buffer_ptr,
                                      lpdword_t buffer_size_ptr,
                                      lpvoid_t overlapped_ptr) {
  assert_not_null(buffer_size_ptr);
  uint32_t buffer_size = *buffer_size_ptr;
  auto content_data = XCONTENT_DATA(content_data_ptr);

  XELOGD("XamContentGetThumbnail(%d, %.8X, %.8X, %.8X(%d), %.8X)", user_index,
         content_data_ptr, buffer_ptr, buffer_size_ptr, buffer_size,
         overlapped_ptr);

  // Get thumbnail (if it exists).
  std::vector<uint8_t> buffer;
  auto result = kernel_state()->content_manager()->GetContentThumbnail(
      content_data, &buffer);

  *buffer_size_ptr = uint32_t(buffer.size());

  if (XSUCCEEDED(result)) {
    // Write data, if we were given a pointer.
    // This may have just been a size query.
    if (buffer_ptr) {
      if (buffer_size < buffer.size()) {
        // Dest buffer too small.
        result = X_ERROR_INSUFFICIENT_BUFFER;
      } else {
        // Copy data.
        std::memcpy(buffer_ptr, buffer.data(), buffer.size());
      }
    }
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT(XamContentGetThumbnail, ExportTag::kImplemented);

dword_result_t XamContentSetThumbnail(dword_t user_index,
                                      lpvoid_t content_data_ptr,
                                      lpvoid_t buffer_ptr, dword_t buffer_size,
                                      lpvoid_t overlapped_ptr) {
  auto content_data = XCONTENT_DATA(content_data_ptr);

  XELOGD("XamContentSetThumbnail(%d, %.8X, %.8X, %d, %.8X)", user_index,
         content_data_ptr, buffer_ptr, buffer_size, overlapped_ptr);

  // Buffer is PNG data.
  auto buffer =
      std::vector<uint8_t>((uint8_t*)buffer_ptr.host_address(),
                           (uint8_t*)buffer_ptr.host_address() + buffer_size);
  auto result = kernel_state()->content_manager()->SetContentThumbnail(
      content_data, std::move(buffer));

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT(XamContentSetThumbnail, ExportTag::kImplemented);

dword_result_t XamContentDelete(dword_t user_index, lpvoid_t content_data_ptr,
                                lpvoid_t overlapped_ptr) {
  auto content_data = XCONTENT_DATA(content_data_ptr);

  XELOGD("XamContentDelete(%d, %.8X, %.8X)", user_index, content_data_ptr,
         overlapped_ptr);

  auto result = kernel_state()->content_manager()->DeleteContent(content_data);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT(XamContentDelete, ExportTag::kImplemented);

void RegisterContentExports(xe::cpu::ExportResolver* export_resolver,
                            KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
