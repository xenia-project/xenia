/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_DEVICES_HOST_PATH_ENTRY_H_
#define XENIA_KERNEL_FS_DEVICES_HOST_PATH_ENTRY_H_

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/kernel/fs/entry.h>

namespace xe {
namespace kernel {
namespace fs {

class HostPathEntry : public Entry {
 public:
  HostPathEntry(Type type, Device* device, const char* path,
                const std::wstring& local_path);
  virtual ~HostPathEntry();

  const std::wstring& local_path() { return local_path_; }

  virtual X_STATUS QueryInfo(XFileInfo* out_info);
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info, size_t length,
                                  const char* file_name, bool restart);

  virtual bool can_map() { return true; }
  virtual std::unique_ptr<MemoryMapping> CreateMemoryMapping(
      Mode map_mode, const size_t offset, const size_t length);

  virtual X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                        XFile** out_file);

 private:
  std::wstring local_path_;
  HANDLE find_file_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_DEVICES_HOST_PATH_ENTRY_H_
