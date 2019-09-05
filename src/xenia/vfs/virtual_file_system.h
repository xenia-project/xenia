/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_VIRTUAL_FILE_SYSTEM_H_
#define XENIA_VFS_VIRTUAL_FILE_SYSTEM_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {

class VirtualFileSystem {
 public:
  VirtualFileSystem();
  ~VirtualFileSystem();
  Entry* ResolveDevice(const std::string& path);

  bool RegisterDevice(std::unique_ptr<Device> device);
  bool UnregisterDevice(const std::string& path);

  bool RegisterSymbolicLink(const std::string& path, const std::string& target);
  bool UnregisterSymbolicLink(const std::string& path);
  bool IsSymbolicLink(const std::string& path);
  bool FindSymbolicLink(const std::string& path, std::string& target);

  Entry* ResolvePath(const std::string& path);
  Entry* ResolveBasePath(const std::string& path);

  Entry* CreatePath(const std::string& path, uint32_t attributes);
  bool DeletePath(const std::string& path);

  X_STATUS OpenFile(const std::string& path,
                    FileDisposition creation_disposition,
                    uint32_t desired_access, bool is_directory, File** out_file,
                    FileAction* out_action);

 private:
  xe::global_critical_region global_critical_region_;
  std::vector<std::unique_ptr<Device>> devices_;
  std::unordered_map<std::string, std::string> symlinks_;

  bool ResolveSymbolicLink(const std::string& path, std::string& result);
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_VIRTUAL_FILE_SYSTEM_H_
