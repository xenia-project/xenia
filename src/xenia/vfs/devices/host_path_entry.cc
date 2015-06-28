/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_entry.h"

#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/vfs/devices/host_path_file.h"

namespace xe {
namespace vfs {

HostPathEntry::HostPathEntry(Device* device, std::string path,
                             const std::wstring& local_path)
    : Entry(device, path), local_path_(local_path) {}

HostPathEntry::~HostPathEntry() = default;

X_STATUS HostPathEntry::Open(KernelState* kernel_state, Mode mode, bool async,
                             XFile** out_file) {
  // TODO(benvanik): plumb through proper disposition/access mode.
  DWORD desired_access =
      is_read_only() ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
  if (mode == Mode::READ_APPEND) {
    desired_access |= FILE_APPEND_DATA;
  }
  DWORD share_mode = FILE_SHARE_READ;
  DWORD creation_disposition;
  switch (mode) {
    case Mode::READ:
      creation_disposition = OPEN_EXISTING;
      break;
    case Mode::READ_WRITE:
      creation_disposition = OPEN_ALWAYS;
      break;
    case Mode::READ_APPEND:
      creation_disposition = OPEN_EXISTING;
      break;
    default:
      assert_unhandled_case(mode);
      break;
  }
  DWORD flags_and_attributes = async ? FILE_FLAG_OVERLAPPED : 0;
  HANDLE file =
      CreateFile(local_path_.c_str(), desired_access, share_mode, NULL,
                 creation_disposition,
                 flags_and_attributes | FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (file == INVALID_HANDLE_VALUE) {
    // TODO(benvanik): pick correct response.
    return X_STATUS_NO_SUCH_FILE;
  }

  *out_file = new HostPathFile(kernel_state, mode, this, file);
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> HostPathEntry::OpenMapped(MappedMemory::Mode mode,
                                                        size_t offset,
                                                        size_t length) {
  return MappedMemory::Open(local_path_, mode, offset, length);
}

}  // namespace vfs
}  // namespace xe
