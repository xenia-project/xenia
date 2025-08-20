/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_CONTAINER_DEVICE_H_
#define XENIA_VFS_DEVICES_XCONTENT_CONTAINER_DEVICE_H_

#include <filesystem>
#include <map>
#include <string_view>

#include "xenia/base/math.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/devices/stfs_xbox.h"

namespace xe {
namespace vfs {
class XContentContainerDevice : public Device {
 public:
  const static uint32_t kBlockSize = 0x1000;

  static std::unique_ptr<Device> CreateContentDevice(
      const std::string_view mount_path,
      const std::filesystem::path& host_path);

  ~XContentContainerDevice() override;

  bool Initialize();

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }

  uint32_t sectors_per_allocation_unit() const override { return 8; }
  uint32_t bytes_per_sector() const override { return 0x200; }

  size_t data_size() const {
    if (header_->content_header.header_size) {
      return files_total_size_ -
             xe::round_up(header_->content_header.header_size, kBlockSize);
    }
    return files_total_size_ - sizeof(XContentContainerHeader);
  }

 protected:
  XContentContainerDevice(
      const std::string_view mount_path, const std::filesystem::path& host_path,
      const std::unique_ptr<XContentContainerHeader> header);
  enum class Result {
    kSuccess = 0,
    kOutOfMemory = -1,
    kReadError = -10,
    kFileMismatch = -30,
    kDamagedFile = -31,
    kTooSmall = -32,
  };

  virtual Result Read() = 0;
  // Load all host files. Usually STFS is only 1 file, meanwhile SVOD is usually
  // multiple file.
  virtual Result LoadHostFiles(FILE* header_file) = 0;

  Entry* ResolvePath(const std::string_view path);
  void CloseFiles();
  void Dump(StringBuffer* string_buffer);

  void SetName(std::string name) { name_ = name; }
  const std::string& GetName() const { return name_; }

  void SetFilesSize(uint64_t files_size) { files_total_size_ = files_size; }
  const uint64_t GetFilesSize() const { return files_total_size_; }

  const std::filesystem::path& GetHostPath() const { return host_path_; }

  const XContentContainerHeader* GetContainerHeader() const {
    return header_.get();
  }

  std::string name_;
  std::filesystem::path host_path_;

  std::map<size_t, FILE*> files_;
  size_t files_total_size_;
  std::unique_ptr<Entry> root_entry_;
  std::unique_ptr<XContentContainerHeader> header_;

 private:
  static std::unique_ptr<XContentContainerHeader> ReadContainerHeader(
      FILE* host_file);
};

}  // namespace vfs
}  // namespace xe

#endif
