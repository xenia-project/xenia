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
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class VirtualFileSystem {
 public:
  VirtualFileSystem();
  ~VirtualFileSystem();

  bool RegisterDevice(std::unique_ptr<Device> device);

  bool RegisterSymbolicLink(std::string path, std::string target);
  bool UnregisterSymbolicLink(std::string path);

  Entry* ResolvePath(std::string path);
  Entry* ResolveBasePath(std::string path);

  Entry* CreatePath(std::string path, uint32_t attributes);
  bool DeletePath(std::string path);

  X_STATUS OpenFile(kernel::KernelState* kernel_state, std::string path,
                    FileDisposition creation_disposition,
                    uint32_t desired_access,
                    kernel::object_ref<kernel::XFile>* out_file,
                    FileAction* out_action);

 private:
  xe::global_critical_region global_critical_region_;
  std::vector<std::unique_ptr<Device>> devices_;
  std::unordered_map<std::string, std::string> symlinks_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_VIRTUAL_FILE_SYSTEM_H_
