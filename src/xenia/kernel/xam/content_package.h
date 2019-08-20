/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_CONTENT_PACKAGE_H_
#define XENIA_KERNEL_XAM_CONTENT_PACKAGE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/vfs/devices/stfs_container_device.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

struct XCONTENT_DATA;

class ContentPackage {
 public:
  ContentPackage(KernelState* kernel_state, const XCONTENT_DATA& data,
                 std::wstring package_path);
  virtual ~ContentPackage();

  virtual bool Mount(std::string root_name) = 0;
  virtual X_RESULT GetThumbnail(std::vector<uint8_t>* buffer) = 0;
  virtual X_RESULT SetThumbnail(std::vector<uint8_t> buffer) = 0;
  virtual X_RESULT Delete() = 0;

  bool Unmount();  // Unmounts device & all root names - returns true if device
                   // is unmounted
  bool Unmount(
      std::string root_name);  // Unmounts a single root name, if no root names
                               // are left mounted the device will be unmounted
                               // - returns true if device is unmounted

  std::wstring package_path() { return package_path_; }
  const std::vector<std::string>& root_names() { return root_names_; }

 protected:
  bool Mount(std::string root_name, std::unique_ptr<vfs::Device> device);

  bool device_inited_ = false;
  KernelState* kernel_state_;
  std::string device_path_;
  std::wstring package_path_;
  std::vector<std::string> root_names_;
};

class StfsContentPackage : public ContentPackage {
 public:
  StfsContentPackage(KernelState* kernel_state, const XCONTENT_DATA& data,
                     std::wstring package_path)
      : ContentPackage(kernel_state, data, package_path) {}

  bool Mount(std::string root_name);
  X_RESULT GetThumbnail(std::vector<uint8_t>* buffer);
  X_RESULT SetThumbnail(std::vector<uint8_t> buffer);
  X_RESULT Delete();

 private:
  vfs::StfsHeader header_;
};

class FolderContentPackage : public ContentPackage {
 public:
  FolderContentPackage(KernelState* kernel_state, const XCONTENT_DATA& data,
                       std::wstring package_path)
      : ContentPackage(kernel_state, data, package_path) {}

  bool Mount(std::string root_name);
  X_RESULT GetThumbnail(std::vector<uint8_t>* buffer);
  X_RESULT SetThumbnail(std::vector<uint8_t> buffer);
  X_RESULT Delete();
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_CONTENT_PACKAGE_H_
