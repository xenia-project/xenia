/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_FILESYSTEM_H_
#define XENIA_KERNEL_FS_FILESYSTEM_H_

#include <string>
#include <unordered_map>
#include <vector>

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/fs/entry.h>

namespace xe {
namespace kernel {
namespace fs {

class Device;

enum class FileSystemType {
  STFS_TITLE,
  DISC_IMAGE,
  XEX_FILE,
};

class FileSystem {
 public:
  FileSystem();
  ~FileSystem();

  FileSystemType InferType(const std::wstring& local_path);
  int InitializeFromPath(FileSystemType type, const std::wstring& local_path);

  int RegisterDevice(const std::string& path, Device* device);
  int RegisterHostPathDevice(const std::string& path,
                             const std::wstring& local_path);
  int RegisterDiscImageDevice(const std::string& path,
                              const std::wstring& local_path);
  int RegisterSTFSContainerDevice(const std::string& path,
                                  const std::wstring& local_path);

  int CreateSymbolicLink(const char* path, const char* target);
  int DeleteSymbolicLink(const char* path);

  Entry* ResolvePath(const char* path);

 private:
  std::vector<Device*> devices_;
  std::unordered_map<std::string, std::string> symlinks_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_FILESYSTEM_H_
