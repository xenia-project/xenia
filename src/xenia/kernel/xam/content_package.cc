/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/content_package.h"

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"

namespace xe {
namespace kernel {
namespace xam {

static const wchar_t* kThumbnailFileName = L"__thumbnail.png";

static int content_device_id_ = 0;

ContentPackage::ContentPackage(KernelState* kernel_state,
                               const XCONTENT_DATA& data,
                               std::wstring package_path)
    : kernel_state_(kernel_state), package_path_(std::move(package_path)) {
  device_path_ = std::string("\\Device\\Content\\") +
                 std::to_string(++content_device_id_) + "\\";
}

ContentPackage::~ContentPackage() { Unmount(); }

bool ContentPackage::Mount(std::string root_name,
                           std::unique_ptr<vfs::Device> device) {
  auto fs = kernel_state_->file_system();

  if (device) {
    if (!fs->RegisterDevice(std::move(device))) {
      return false;
    }
  }

  if (!fs->RegisterSymbolicLink(root_name + ":", device_path_)) {
    return false;
  }

  root_names_.push_back(root_name);

  return true;
}

bool ContentPackage::Unmount() {
  auto fs = kernel_state_->file_system();
  for (auto root_name : root_names_) {
    fs->UnregisterSymbolicLink(root_name + ":");
  }
  root_names_.clear();
  fs->UnregisterDevice(device_path_);
  device_inited_ = false;

  return true;
}

bool ContentPackage::Unmount(std::string root_name) {
  auto itr = std::find(root_names_.begin(), root_names_.end(), root_name);
  if (itr != root_names_.end()) {
    root_names_.erase(itr);
    kernel_state_->file_system()->UnregisterSymbolicLink(root_name + ":");
  }

  // Do a complete device unmount if no root names are left
  if (!root_names_.size()) {
    return Unmount();
  }
  return false;  // still mounted under a different root_name, so return false
}

bool FolderContentPackage::Mount(std::string root_name) {
  std::unique_ptr<vfs::HostPathDevice> device = nullptr;

  if (!device_inited_) {
    device = std::make_unique<vfs::HostPathDevice>(device_path_, package_path_,
                                                   false);
    if (!device->Initialize()) {
      return false;
    }
    device_inited_ = true;
  }

  return ContentPackage::Mount(root_name, std::move(device));
}

X_RESULT FolderContentPackage::GetThumbnail(std::vector<uint8_t>* buffer) {
  // Grab thumb from kThumbnailFileName if it exists, otherwise try STFS headers
  auto thumb_path = xe::join_paths(package_path_, kThumbnailFileName);
  if (xe::filesystem::PathExists(thumb_path)) {
    auto file = xe::filesystem::OpenFile(thumb_path, "rb");
    fseek(file, 0, SEEK_END);
    size_t file_len = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer->resize(file_len);
    fread(const_cast<uint8_t*>(buffer->data()), 1, buffer->size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  }
  auto result = X_ERROR_FILE_NOT_FOUND;

  // Try reading thumbnail from kStfsHeadersExtension file
  auto headers_path = package_path_ + ContentManager::kStfsHeadersExtension;
  if (xe::filesystem::PathExists(headers_path)) {
    vfs::StfsHeader* header =
        new vfs::StfsHeader();  // huge class, alloc on heap
    auto map = MappedMemory::Open(headers_path, MappedMemory::Mode::kRead, 0,
                                  vfs::StfsHeader::kHeaderLength);
    if (map) {
      if (header->Read(map->data())) {
        buffer->resize(header->thumbnail_image_size);
        memcpy(buffer->data(), header->thumbnail_image,
               header->thumbnail_image_size);
        result = X_ERROR_SUCCESS;
      }
    }
    delete header;
  }
  return result;
}

X_RESULT FolderContentPackage::SetThumbnail(std::vector<uint8_t> buffer) {
  xe::filesystem::CreateFolder(package_path_);
  if (xe::filesystem::PathExists(package_path_)) {
    auto thumb_path = xe::join_paths(package_path_, kThumbnailFileName);
    auto file = xe::filesystem::OpenFile(thumb_path, "wb");
    fwrite(buffer.data(), 1, buffer.size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  }
  return X_ERROR_FILE_NOT_FOUND;
}

X_RESULT FolderContentPackage::Delete() {
  // Make sure package isn't in use
  Unmount();

  if (xe::filesystem::PathExists(package_path_)) {
    xe::filesystem::DeleteFolder(package_path_);
    return X_ERROR_SUCCESS;
  }
  return X_ERROR_FILE_NOT_FOUND;
}

bool StfsContentPackage::Mount(std::string root_name) {
  std::unique_ptr<vfs::StfsContainerDevice> device = nullptr;

  if (!device_inited_) {
    device =
        std::make_unique<vfs::StfsContainerDevice>(device_path_, package_path_);
    if (!device->Initialize()) {
      return false;
    }

    header_ = device->header();
    device_inited_ = true;
  }

  return ContentPackage::Mount(root_name, std::move(device));
}

X_RESULT StfsContentPackage::GetThumbnail(std::vector<uint8_t>* buffer) {
  if (!device_inited_) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  buffer->resize(header_.thumbnail_image_size);
  memcpy(buffer->data(), header_.thumbnail_image, header_.thumbnail_image_size);
  return X_ERROR_SUCCESS;
}

X_RESULT StfsContentPackage::SetThumbnail(std::vector<uint8_t> buffer) {
  return X_ERROR_FUNCTION_FAILED;  // can't write to STFS headers right now
}

X_RESULT StfsContentPackage::Delete() {
  // Make sure package isn't in use
  Unmount();

  if (xe::filesystem::PathExists(package_path_)) {
    return xe::filesystem::DeleteFile(package_path_) ? X_ERROR_SUCCESS
                                                     : X_ERROR_FUNCTION_FAILED;
  }
  return X_ERROR_FILE_NOT_FOUND;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
