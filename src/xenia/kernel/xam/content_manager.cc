/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/content_manager.h"

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"

namespace xe {
namespace kernel {
namespace xam {

static const wchar_t* kThumbnailFileName = L"__thumbnail.png";

static const wchar_t* kGameUserContentDirName = L"profile";

static int content_device_id_ = 0;

ContentPackage::ContentPackage(KernelState* kernel_state, std::string root_name,
                               const XCONTENT_DATA& data,
                               std::wstring package_path)
    : kernel_state_(kernel_state), root_name_(std::move(root_name)) {
  device_path_ = std::string("\\Device\\Content\\") +
                 std::to_string(++content_device_id_) + "\\";

  auto fs = kernel_state_->file_system();

  std::unique_ptr<vfs::Device> device;

  // If this isn't a folder try mounting as STFS package
  // Otherwise mount as a local host path
  if (filesystem::PathExists(package_path) &&
      !filesystem::IsFolder(package_path)) {
    device =
        std::make_unique<vfs::StfsContainerDevice>(device_path_, package_path);
  } else {
    device = std::make_unique<vfs::HostPathDevice>(device_path_, package_path,
                                                   false);
  }

  device->Initialize();
  fs->RegisterDevice(std::move(device));
  fs->RegisterSymbolicLink(root_name_ + ":", device_path_);
}

ContentPackage::~ContentPackage() {
  auto fs = kernel_state_->file_system();
  fs->UnregisterSymbolicLink(root_name_ + ":");
  fs->UnregisterDevice(device_path_);
}

ContentManager::ContentManager(KernelState* kernel_state,
                               std::wstring root_path)
    : kernel_state_(kernel_state), root_path_(std::move(root_path)) {}

ContentManager::~ContentManager() = default;

uint32_t ContentManager::title_id() {
  if (title_id_override_) {
    return title_id_override_;
  }
  if (!kernel_state_->GetExecutableModule()) {
    return -1;
  }
  return kernel_state_->title_id();
}

std::wstring ContentManager::ResolvePackageRoot(uint32_t content_type) {
  wchar_t title_id_str[9] = L"00000000";
  std::swprintf(title_id_str, 9, L"%.8X", title_id());

  wchar_t content_type_str[9] = L"00000000";
  std::swprintf(content_type_str, 9, L"%.8X", content_type);

  // Package root path:
  // content_root/title_id/type_name/
  auto package_root = xe::join_paths(
      root_path_, xe::join_paths(title_id_str, content_type_str));
  return package_root + xe::kWPathSeparator;
}

std::wstring ContentManager::ResolvePackagePath(const XCONTENT_DATA& data) {
  // Content path:
  // content_root/title_id/type_name/data_file_name/
  auto package_root = ResolvePackageRoot(data.content_type);
  auto package_path =
      xe::join_paths(package_root, xe::to_wstring(data.file_name));

  // Add slash to end of path if this is a folder
  // (or package doesn't exist, meaning we're creating a new folder)
  if (!xe::filesystem::PathExists(package_path) ||
      xe::filesystem::IsFolder(package_path)) {
    package_path += xe::kPathSeparator;
  }
  return package_path;
}

std::vector<XCONTENT_DATA> ContentManager::ListContent(uint32_t device_id,
                                                       uint32_t content_type) {
  std::vector<XCONTENT_DATA> result;

  // Search path:
  // content_root/title_id/type_name/*
  auto package_root = ResolvePackageRoot(content_type);
  auto file_infos = xe::filesystem::ListFiles(package_root);
  for (const auto& file_info : file_infos) {
    XCONTENT_DATA content_data;
    content_data.device_id = device_id;
    content_data.content_type = content_type;
    content_data.display_name = file_info.name;
    content_data.file_name = xe::to_string(file_info.name);
    result.emplace_back(std::move(content_data));
  }

  return result;
}

std::unique_ptr<ContentPackage> ContentManager::ResolvePackage(
    std::string root_name, const XCONTENT_DATA& data) {
  auto package_path = ResolvePackagePath(data);
  if (!xe::filesystem::PathExists(package_path)) {
    return nullptr;
  }

  auto global_lock = global_critical_region_.Acquire();

  auto package = std::make_unique<ContentPackage>(kernel_state_, root_name,
                                                  data, package_path);
  return package;
}

bool ContentManager::ContentExists(const XCONTENT_DATA& data) {
  auto path = ResolvePackagePath(data);
  return xe::filesystem::PathExists(path);
}

X_RESULT ContentManager::CreateContent(std::string root_name,
                                       const XCONTENT_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(root_name)) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(data);
  if (xe::filesystem::PathExists(package_path)) {
    // Exists, must not!
    return X_ERROR_ALREADY_EXISTS;
  }

  if (!xe::filesystem::CreateFolder(package_path)) {
    return X_ERROR_ACCESS_DENIED;
  }

  auto package = ResolvePackage(root_name, data);
  assert_not_null(package);

  open_packages_.insert({root_name, package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::OpenContent(std::string root_name,
                                     const XCONTENT_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(root_name)) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(data);
  if (!xe::filesystem::PathExists(package_path)) {
    // Does not exist, must be created.
    return X_ERROR_FILE_NOT_FOUND;
  }

  // Open package.
  auto package = ResolvePackage(root_name, data);
  assert_not_null(package);

  open_packages_.insert({root_name, package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::CloseContent(std::string root_name) {
  auto global_lock = global_critical_region_.Acquire();

  auto it = open_packages_.find(root_name);
  if (it == open_packages_.end()) {
    return X_ERROR_FILE_NOT_FOUND;
  }

  auto package = it->second;
  open_packages_.erase(it);
  delete package;

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::GetContentThumbnail(const XCONTENT_DATA& data,
                                             std::vector<uint8_t>* buffer) {
  auto global_lock = global_critical_region_.Acquire();
  auto package_path = ResolvePackagePath(data);
  auto thumb_path = xe::join_paths(package_path, kThumbnailFileName);
  if (xe::filesystem::PathExists(thumb_path)) {
    auto file = xe::filesystem::OpenFile(thumb_path, "rb");
    fseek(file, 0, SEEK_END);
    size_t file_len = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer->resize(file_len);
    fread(const_cast<uint8_t*>(buffer->data()), 1, buffer->size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::SetContentThumbnail(const XCONTENT_DATA& data,
                                             std::vector<uint8_t> buffer) {
  auto global_lock = global_critical_region_.Acquire();
  auto package_path = ResolvePackagePath(data);
  xe::filesystem::CreateFolder(package_path);
  if (xe::filesystem::PathExists(package_path)) {
    auto thumb_path = xe::join_paths(package_path, kThumbnailFileName);
    auto file = xe::filesystem::OpenFile(thumb_path, "wb");
    fwrite(buffer.data(), 1, buffer.size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::DeleteContent(const XCONTENT_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  auto package_path = ResolvePackagePath(data);
  if (xe::filesystem::PathExists(package_path)) {
    if (xe::filesystem::IsFolder(package_path)) {
      xe::filesystem::DeleteFolder(package_path);
    } else {
      // TODO: delete STFS package?
    }
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

std::wstring ContentManager::ResolveGameUserContentPath() {
  wchar_t title_id[9] = L"00000000";
  std::swprintf(title_id, 9, L"%.8X", kernel_state_->title_id());
  auto user_name = xe::to_wstring(kernel_state_->user_profile()->name());

  // Per-game per-profile data location:
  // content_root/title_id/profile/user_name
  auto package_root = xe::join_paths(
      root_path_,
      xe::join_paths(title_id,
                     xe::join_paths(kGameUserContentDirName, user_name)));
  return package_root + xe::kWPathSeparator;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
