/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/base/logging.h"
#include "xenia/vfs/devices/xcontent_devices/stfs/stfs_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/svod/svod_container_device.h"

namespace xe {
namespace vfs {

std::unique_ptr<Device> XContentContainerDevice::CreateContentDevice(
    const std::string_view mount_path, const std::filesystem::path& host_path) {
  if (!std::filesystem::exists(host_path)) {
    XELOGE("XContent: Path to XContent container does not exist: {}",
           xe::path_to_utf8(host_path));
    return nullptr;
  }

  if (std::filesystem::is_directory(host_path)) {
    return nullptr;
  }

  FILE* host_file = xe::filesystem::OpenFile(host_path, "rb");
  if (!host_file) {
    XELOGE("XContent: Error opening XContent file.");
    return nullptr;
  }

  const uint64_t package_size = std::filesystem::file_size(host_path);
  if (package_size < sizeof(XContentContainerHeader)) {
    return nullptr;
  }

  auto header = XContentContainerDevice::ReadContainerHeader(host_file);
  if (header == nullptr) {
    return nullptr;
  }

  fclose(host_file);

  if (!header->content_header.is_magic_valid()) {
    XELOGE("XContent: Invalid package magic: {:08X}",
           static_cast<uint32_t>(header->content_header.magic.get()));
    return nullptr;
  }

  switch (header->content_metadata.volume_type) {
    case XContentVolumeType::kStfs:
      return std::make_unique<StfsContainerDevice>(mount_path, host_path,
                                                   std::move(header));
      break;
    case XContentVolumeType::kSvod:
      return std::make_unique<SvodContainerDevice>(mount_path, host_path,
                                                   std::move(header));
      break;
    default:
      break;
  }

  return nullptr;
}

XContentContainerDevice::XContentContainerDevice(
    const std::string_view mount_path, const std::filesystem::path& host_path,
    std::unique_ptr<XContentContainerHeader> header)
    : Device(mount_path),
      name_("XContent"),
      host_path_(host_path),
      files_total_size_(0),
      header_(std::move(header)) {}

XContentContainerDevice::~XContentContainerDevice() {}

bool XContentContainerDevice::Initialize() {
  XELOGI("Loading XContent file: {}", xe::path_to_utf8(host_path_));
  auto header_file = xe::filesystem::OpenFile(host_path_, "rb");
  if (!header_file) {
    XELOGE("Error opening XContent file.");
    return false;
  }

  if (LoadHostFiles(header_file) != Result::kSuccess) {
    XELOGE("Error loading XContent host files.");
    return false;
  }

  return Read() == Result::kSuccess;
}

std::unique_ptr<XContentContainerHeader>
XContentContainerDevice::ReadContainerHeader(FILE* host_file) {
  std::unique_ptr<XContentContainerHeader> header =
      std::make_unique<XContentContainerHeader>();
  // Read header & check signature
  if (fread(header.get(), sizeof(XContentContainerHeader), 1, host_file) != 1) {
    return nullptr;
  }
  return header;
}

Entry* XContentContainerDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("StfsContainerDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

void XContentContainerDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

void XContentContainerDevice::CloseFiles() {
  for (auto& file : files_) {
    fclose(file.second);
  }
  files_.clear();
  files_total_size_ = 0;
}

}  // namespace vfs
}  // namespace xe