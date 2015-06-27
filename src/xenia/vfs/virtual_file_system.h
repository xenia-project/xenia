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

#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class Device;

enum class FileSystemType {
  STFS_TITLE,
  DISC_IMAGE,
  XEX_FILE,
};

class VirtualFileSystem {
 public:
  VirtualFileSystem();
  ~VirtualFileSystem();

  FileSystemType InferType(const std::wstring& local_path);
  int InitializeFromPath(FileSystemType type, const std::wstring& local_path);

  int RegisterDevice(const std::string& path, Device* device);
  int RegisterHostPathDevice(const std::string& path,
                             const std::wstring& local_path, bool read_only);
  int RegisterDiscImageDevice(const std::string& path,
                              const std::wstring& local_path);
  int RegisterSTFSContainerDevice(const std::string& path,
                                  const std::wstring& local_path);

  int CreateSymbolicLink(const std::string& path, const std::string& target);
  int DeleteSymbolicLink(const std::string& path);

  std::unique_ptr<Entry> ResolvePath(const std::string& path);
  X_STATUS Open(std::unique_ptr<Entry> entry, KernelState* kernel_state,
                Mode mode, bool async, XFile** out_file);

 private:
  std::vector<Device*> devices_;
  std::unordered_map<std::string, std::string> symlinks_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_VIRTUAL_FILE_SYSTEM_H_
