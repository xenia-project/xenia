/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/content_manager.h"

#include <string>

#include "xenia/base/fs.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {

static const wchar_t* kThumbnailFileName = L"__thumbnail.png";

static int content_device_id_ = 0;

ContentPackage::ContentPackage(KernelState* kernel_state, std::string root_name,
                               const XCONTENT_DATA& data,
                               std::wstring package_path)
    : kernel_state_(kernel_state), root_name_(std::move(root_name)) {
  device_path_ = std::string("\\Device\\Content\\") +
                 std::to_string(++content_device_id_) + "\\";
  kernel_state_->file_system()->RegisterHostPathDevice(device_path_,
                                                       package_path, false);
  kernel_state_->file_system()->CreateSymbolicLink(root_name_ + ":",
                                                   device_path_);
}

ContentPackage::~ContentPackage() {
  kernel_state_->file_system()->DeleteSymbolicLink(root_name_ + ":");
  // TODO(benvanik): unregister device.
}

ContentManager::ContentManager(KernelState* kernel_state,
                               std::wstring root_path)
    : kernel_state_(kernel_state), root_path_(std::move(root_path)) {}

ContentManager::~ContentManager() = default;

std::wstring ContentManager::ResolvePackageRoot(uint32_t content_type) {
  wchar_t title_id[9] = L"00000000";
  std::swprintf(title_id, 9, L"%.8X", kernel_state_->title_id());

  std::wstring type_name;
  switch (content_type) {
    case 1:
      // Save games.
      type_name = L"00000001";
      break;
    case 2:
      // DLC from the marketplace.
      type_name = L"00000002";
      break;
    case 3:
      // Publisher content?
      type_name = L"00000003";
      break;
    case 0x000D0000:
      // ???
      type_name = L"000D0000";
      break;
    default:
      assert_unhandled_case(data.content_type);
      return nullptr;
  }

  // Package root path:
  // content_root/title_id/type_name/
  auto package_root =
      xe::join_paths(root_path_, xe::join_paths(title_id, type_name));
  return package_root + L"\\";
}

std::wstring ContentManager::ResolvePackagePath(const XCONTENT_DATA& data) {
  // Content path:
  // content_root/title_id/type_name/data_file_name/
  auto package_root = ResolvePackageRoot(data.content_type);
  auto package_path =
      xe::join_paths(package_root, xe::to_wstring(data.file_name));
  package_path += xe::path_separator;
  return package_path;
}

std::vector<XCONTENT_DATA> ContentManager::ListContent(uint32_t device_id,
                                                       uint32_t content_type) {
  std::vector<XCONTENT_DATA> result;

  // Search path:
  // content_root/title_id/type_name/*
  auto package_root = ResolvePackageRoot(content_type);
  auto file_infos = xe::fs::ListFiles(package_root);
  for (const auto& file_info : file_infos) {
    if (file_info.type != xe::fs::FileInfo::Type::kDirectory) {
      // Directories only.
      continue;
    }
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
  if (!xe::fs::PathExists(package_path)) {
    return nullptr;
  }

  std::lock_guard<std::recursive_mutex> lock(content_mutex_);

  auto package = std::make_unique<ContentPackage>(kernel_state_, root_name,
                                                  data, package_path);
  return package;
}

bool ContentManager::ContentExists(const XCONTENT_DATA& data) {
  auto path = ResolvePackagePath(data);
  return xe::fs::PathExists(path);
}

X_RESULT ContentManager::CreateContent(std::string root_name,
                                       const XCONTENT_DATA& data) {
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);

  if (open_packages_.count(root_name)) {
    // Already content open with this root name.
    return X_ERROR_INVALID_NAME;
  }

  auto package_path = ResolvePackagePath(data);
  if (xe::fs::PathExists(package_path)) {
    // Exists, must not!
    return X_ERROR_ALREADY_EXISTS;
  }

  if (!xe::fs::CreateFolder(package_path)) {
    return X_ERROR_ACCESS_DENIED;
  }

  auto package = ResolvePackage(root_name, data);
  assert_not_null(package);

  open_packages_.insert({root_name, package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::OpenContent(std::string root_name,
                                     const XCONTENT_DATA& data) {
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);

  if (open_packages_.count(root_name)) {
    // Already content open with this root name.
    return X_ERROR_INVALID_NAME;
  }

  auto package_path = ResolvePackagePath(data);
  if (!xe::fs::PathExists(package_path)) {
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
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);

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
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);
  auto package_path = ResolvePackagePath(data);
  auto thumb_path = xe::join_paths(package_path, kThumbnailFileName);
  if (xe::fs::PathExists(thumb_path)) {
    auto file = _wfopen(thumb_path.c_str(), L"rb");
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
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);
  auto package_path = ResolvePackagePath(data);
  if (xe::fs::PathExists(package_path)) {
    auto thumb_path = xe::join_paths(package_path, kThumbnailFileName);
    auto file = _wfopen(thumb_path.c_str(), L"wb");
    fwrite(buffer.data(), 1, buffer.size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::DeleteContent(const XCONTENT_DATA& data) {
  std::lock_guard<std::recursive_mutex> lock(content_mutex_);

  auto package_path = ResolvePackagePath(data);
  if (xe::fs::PathExists(package_path)) {
    xe::fs::DeleteFolder(package_path);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

}  // namespace kernel
}  // namespace xe
