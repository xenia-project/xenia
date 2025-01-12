/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/content_manager.h"

#include <array>
#include <string>
#include <unordered_set>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/devices/host_path_device.h"

DECLARE_int32(license_mask);

namespace xe {
namespace kernel {
namespace xam {

static const char* kThumbnailFileName = "__thumbnail.png";
static const char* kGameContentHeaderDirName = "Headers";

static int content_device_id_ = 0;

ContentPackage::ContentPackage(KernelState* kernel_state,
                               const std::string_view root_name,
                               const XCONTENT_AGGREGATE_DATA& data,
                               const std::filesystem::path& package_path)
    : kernel_state_(kernel_state), root_name_(root_name) {
  device_path_ = fmt::format("\\Device\\Content\\{0}\\", ++content_device_id_);
  content_data_ = data;

  auto fs = kernel_state_->file_system();
  auto device =
      std::make_unique<vfs::HostPathDevice>(device_path_, package_path, false);
  device->Initialize();
  fs->RegisterDevice(std::move(device));
  fs->RegisterSymbolicLink(root_name_ + ":", device_path_);
}

ContentPackage::~ContentPackage() {
  auto fs = kernel_state_->file_system();
  fs->UnregisterSymbolicLink(root_name_ + ":");
  fs->UnregisterDevice(device_path_);
}

void ContentPackage::LoadPackageLicenseMask(
    const std::filesystem::path header_path) {
  license_ = cvars::license_mask;

  if (!std::filesystem::exists(header_path)) {
    return;
  }

  auto file = xe::filesystem::OpenFile(header_path, "rb");
  auto file_size = std::filesystem::file_size(header_path);
  if (file_size < sizeof(XCONTENT_AGGREGATE_DATA) + sizeof(license_)) {
    fclose(file);
    return;
  }

  fseek(file, sizeof(XCONTENT_AGGREGATE_DATA), SEEK_SET);
  size_t result = fread(&license_, 1, sizeof(license_), file);
  fclose(file);
}

ContentManager::ContentManager(KernelState* kernel_state,
                               const std::filesystem::path& root_path)
    : kernel_state_(kernel_state), root_path_(root_path) {}

ContentManager::~ContentManager() = default;

std::filesystem::path ContentManager::ResolvePackageRoot(
    uint64_t xuid, uint32_t title_id, XContentType content_type) const {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }

  auto xuid_str = fmt::format("{:016X}", xuid);
  auto title_id_str = fmt::format("{:08X}", title_id);
  auto content_type_str =
      fmt::format("{:08X}", static_cast<uint32_t>(content_type));

  // Package root path:
  // content_root/title_id/content_type/
  return root_path_ / xuid_str / title_id_str / content_type_str;
}

std::filesystem::path ContentManager::ResolvePackagePath(
    const uint64_t xuid, const XCONTENT_AGGREGATE_DATA& data,
    const uint32_t disc_number) {
  // Content path:
  // content_root/title_id/content_type/data_file_name/
  auto get_package_path = [&, data, disc_number](const uint32_t title_id) {
    uint64_t used_xuid =
        (data.xuid != -1 && data.xuid != 0) ? data.xuid.get() : xuid;

    // All DLCs are stored in common directory, so we need to override xuid for
    // them and probably some other types.
    if (data.content_type == XContentType::kMarketplaceContent) {
      used_xuid = 0;
    }

    auto package_root =
        ResolvePackageRoot(used_xuid, title_id, data.content_type);
    std::string final_name = xe::string_util::trim(data.file_name());
    std::filesystem::path package_path = package_root / xe::to_path(final_name);

    if (disc_number != -1) {
      package_path /= fmt::format("disc{:03}", disc_number);
    }
    return package_path;
  };

  if (data.content_type == XContentType::kPublisher) {
    const std::unordered_set<uint32_t> title_ids =
        FindPublisherTitleIds(xuid, data.title_id);

    for (const auto& title_id : title_ids) {
      auto package_path = get_package_path(title_id);

      if (!std::filesystem::exists(package_path)) {
        continue;
      }
      return package_path;
    }
  }

  // Default handling for current title
  return get_package_path(data.title_id);
}

std::filesystem::path ContentManager::ResolvePackageHeaderPath(
    const std::string_view file_name, uint64_t xuid, uint32_t title_id,
    const XContentType content_type) const {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }

  if (content_type == XContentType::kMarketplaceContent) {
    xuid = 0;
  }

  auto xuid_str = fmt::format("{:016X}", xuid);
  auto title_id_str = fmt::format("{:08X}", title_id);
  auto content_type_str = fmt::format("{:08X}", uint32_t(content_type));
  std::string final_name =
      xe::string_util::trim(std::string(file_name)) + ".header";

  // Header root path:
  // content_root/xuid/title_id/Headers/content_type/
  return root_path_ / xuid_str / title_id_str / kGameContentHeaderDirName /
         content_type_str / final_name;
}

std::unordered_set<uint32_t> ContentManager::FindPublisherTitleIds(
    const uint64_t xuid, uint32_t base_title_id) const {
  if (base_title_id == kCurrentlyRunningTitleId) {
    base_title_id = kernel_state_->title_id();
  }
  std::unordered_set<uint32_t> title_ids = {};

  std::string publisher_id_regex =
      fmt::format("^{:04X}.*", static_cast<uint16_t>(base_title_id >> 16));
  // Get all publisher entries
  auto publisher_entries = xe::filesystem::FilterByName(
      xe::filesystem::ListDirectories(root_path_ /
                                      fmt::format("{:016X}", xuid)),
      std::regex(publisher_id_regex));

  for (const auto& entry : publisher_entries) {
    std::filesystem::path path_to_publisher_dir =
        entry.path / entry.name /
        fmt::format("{:08X}", static_cast<uint32_t>(XContentType::kPublisher));

    if (!std::filesystem::exists(path_to_publisher_dir)) {
      continue;
    }

    title_ids.insert(xe::string_util::from_string<uint32_t>(
        xe::path_to_utf8(entry.name), true));
  }

  // Always remove current title. It will be handled differently
  if (title_ids.count(base_title_id)) {
    title_ids.erase(base_title_id);
  }
  return title_ids;
}

std::vector<XCONTENT_AGGREGATE_DATA> ContentManager::ListContent(
    const uint32_t device_id, const uint64_t xuid, const uint32_t title_id,
    const XContentType content_type) const {
  std::vector<XCONTENT_AGGREGATE_DATA> result;

  std::unordered_set<uint32_t> title_ids = {title_id};

  if (content_type == XContentType::kPublisher) {
    title_ids = FindPublisherTitleIds(xuid, title_id);
  }

  for (const uint32_t& title_id : title_ids) {
    // Search path:
    // content_root/xuid/title_id/type_name/*
    auto package_root = ResolvePackageRoot(xuid, title_id, content_type);
    auto file_infos = xe::filesystem::ListFiles(package_root);

    for (const auto& file_info : file_infos) {
      if (file_info.type != xe::filesystem::FileInfo::Type::kDirectory) {
        // Directories only.
        continue;
      }

      XCONTENT_AGGREGATE_DATA content_data;
      if (XSUCCEEDED(ReadContentHeaderFile(xe::path_to_utf8(file_info.name),
                                           xuid, title_id, content_type,
                                           content_data))) {
        result.emplace_back(std::move(content_data));
      } else {
        content_data.device_id = device_id;
        content_data.content_type = content_type;
        content_data.set_display_name(xe::path_to_utf16(file_info.name));
        content_data.set_file_name(xe::path_to_utf8(file_info.name));
        content_data.title_id = title_id;
        content_data.xuid = xuid;
        result.emplace_back(std::move(content_data));
      }
    }
  }
  return result;
}

std::unique_ptr<ContentPackage> ContentManager::ResolvePackage(
    const std::string_view root_name, const uint64_t xuid,
    const XCONTENT_AGGREGATE_DATA& data, const uint32_t disc_number) {
  auto package_path = ResolvePackagePath(xuid, data, disc_number);
  if (!std::filesystem::exists(package_path)) {
    return nullptr;
  }

  auto global_lock = global_critical_region_.Acquire();

  auto package = std::make_unique<ContentPackage>(kernel_state_, root_name,
                                                  data, package_path);
  return package;
}

bool ContentManager::ContentExists(const uint64_t xuid,
                                   const XCONTENT_AGGREGATE_DATA& data) {
  auto path = ResolvePackagePath(xuid, data);
  return std::filesystem::exists(path);
}

X_RESULT ContentManager::WriteContentHeaderFile(const uint64_t xuid,
                                                XCONTENT_AGGREGATE_DATA data) {
  if (data.title_id == -1) {
    data.title_id = kernel_state_->title_id();
  }
  if (data.xuid == -1) {
    data.xuid = xuid;
  }
  uint64_t used_xuid =
      (data.xuid != -1 && data.xuid != 0) ? data.xuid.get() : xuid;

  auto header_path = ResolvePackageHeaderPath(data.file_name(), used_xuid,
                                              data.title_id, data.content_type);
  auto parent_path = header_path.parent_path();

  if (!std::filesystem::exists(parent_path)) {
    if (!std::filesystem::create_directories(parent_path)) {
      return X_STATUS_ACCESS_DENIED;
    }
  }

  xe::filesystem::CreateEmptyFile(header_path);

  if (std::filesystem::exists(header_path)) {
    auto file = xe::filesystem::OpenFile(header_path, "wb");
    fwrite(&data, 1, sizeof(XCONTENT_AGGREGATE_DATA), file);
    fclose(file);
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_NO_SUCH_FILE;
}

X_RESULT ContentManager::ReadContentHeaderFile(
    const std::string_view file_name, const uint64_t xuid,
    const uint32_t title_id, XContentType content_type,
    XCONTENT_AGGREGATE_DATA& data) const {
  auto header_file_path =
      ResolvePackageHeaderPath(file_name, xuid, title_id, content_type);
  constexpr uint32_t header_size = sizeof(XCONTENT_AGGREGATE_DATA);

  if (std::filesystem::exists(header_file_path)) {
    auto file = xe::filesystem::OpenFile(header_file_path, "rb");

    std::array<uint8_t, header_size> buffer;

    auto file_size = std::filesystem::file_size(header_file_path);
    if (file_size < header_size) {
      fclose(file);
      return X_STATUS_END_OF_FILE;
    }

    size_t result = fread(buffer.data(), 1, header_size, file);
    if (result != header_size) {
      fclose(file);
      return X_STATUS_END_OF_FILE;
    }

    fclose(file);
    std::memcpy(&data, buffer.data(), buffer.size());
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_NO_SUCH_FILE;
}

X_RESULT ContentManager::CreateContent(const std::string_view root_name,
                                       const uint64_t xuid,
                                       const XCONTENT_AGGREGATE_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(string_key(root_name))) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(xuid, data);
  if (std::filesystem::exists(package_path)) {
    // Exists, must not!
    return X_ERROR_ALREADY_EXISTS;
  }

  if (!std::filesystem::create_directories(package_path)) {
    return X_ERROR_ACCESS_DENIED;
  }

  auto package = ResolvePackage(root_name, xuid, data);
  assert_not_null(package);

  open_packages_.insert({string_key::create(root_name), package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::OpenContent(const std::string_view root_name,
                                     const uint64_t xuid,
                                     const XCONTENT_AGGREGATE_DATA& data,
                                     uint32_t& content_license,
                                     const uint32_t disc_number) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(string_key(root_name))) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(xuid, data, disc_number);
  if (!std::filesystem::exists(package_path)) {
    // Does not exist, must be created.
    return X_ERROR_FILE_NOT_FOUND;
  }

  // Open package.
  auto package = ResolvePackage(root_name, xuid, data, disc_number);
  assert_not_null(package);

  package->LoadPackageLicenseMask(ResolvePackageHeaderPath(
      data.file_name(), xuid, kernel_state_->title_id(), data.content_type));

  content_license = package->GetPackageLicense();

  open_packages_.insert({string_key::create(root_name), package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::CloseContent(const std::string_view root_name) {
  auto global_lock = global_critical_region_.Acquire();

  auto it = open_packages_.find(string_key(root_name));
  if (it == open_packages_.end()) {
    return X_ERROR_FILE_NOT_FOUND;
  }
  CloseOpenedFilesFromContent(root_name);

  auto package = it->second;
  open_packages_.erase(it);
  delete package;

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::GetContentThumbnail(
    const uint64_t xuid, const XCONTENT_AGGREGATE_DATA& data,
    std::vector<uint8_t>* buffer) {
  auto global_lock = global_critical_region_.Acquire();

  auto package_path = ResolvePackagePath(xuid, data);
  auto thumb_path = package_path / kThumbnailFileName;
  if (std::filesystem::exists(thumb_path)) {
    auto file = xe::filesystem::OpenFile(thumb_path, "rb");
    size_t file_len = std::filesystem::file_size(thumb_path);
    buffer->resize(file_len);
    fread(const_cast<uint8_t*>(buffer->data()), 1, buffer->size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::SetContentThumbnail(
    const uint64_t xuid, const XCONTENT_AGGREGATE_DATA& data,
    std::vector<uint8_t> buffer) {
  auto global_lock = global_critical_region_.Acquire();
  auto package_path = ResolvePackagePath(xuid, data);
  std::filesystem::create_directories(package_path);
  if (std::filesystem::exists(package_path)) {
    auto thumb_path = package_path / kThumbnailFileName;
    auto file = xe::filesystem::OpenFile(thumb_path, "wb");
    fwrite(buffer.data(), 1, buffer.size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::DeleteContent(const uint64_t xuid,
                                       const XCONTENT_AGGREGATE_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (IsContentOpen(data)) {
    // TODO(Gliniak): Get real error code for this case.
    return X_ERROR_ACCESS_DENIED;
  }

  auto package_path = ResolvePackagePath(xuid, data);
  if (std::filesystem::remove_all(package_path) > 0) {
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

std::filesystem::path ContentManager::ResolveGameUserContentPath(
    const uint64_t xuid) {
  auto xuid_str = fmt::format("{:016X}", xuid);
  auto title_id = fmt::format("{:08X}", kernel_state_->title_id());

  return root_path_ / xuid_str / kDashboardStringID / "00010000" / xuid_str /
         title_id;
}

bool ContentManager::IsContentOpen(const XCONTENT_AGGREGATE_DATA& data) const {
  return std::any_of(open_packages_.cbegin(), open_packages_.cend(),
                     [data](std::pair<string_key, ContentPackage*> content) {
                       return data == content.second->GetPackageContentData();
                     });
}

void ContentManager::CloseOpenedFilesFromContent(
    const std::string_view root_name) {
  // TODO(Gliniak): Cleanup this code to care only about handles
  // related to provided content
  const std::vector<object_ref<XFile>> all_files_handles =
      kernel_state_->object_table()->GetObjectsByType<XFile>(
          XObject::Type::File);

  std::string resolved_path = "";
  kernel_state_->file_system()->FindSymbolicLink(std::string(root_name) + ':',
                                                 resolved_path);

  for (const object_ref<XFile>& file : all_files_handles) {
    std::string file_path = file->entry()->absolute_path();
    bool is_file_inside_content = utf8::starts_with(file_path, resolved_path);

    if (is_file_inside_content) {
      file->ReleaseHandle();
    }
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
