/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <regex>

#include "xenia/config.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

#include "xenia/patcher/patch_db.h"

DEFINE_bool(apply_patches, true, "Enables custom patching functionality",
            "General");

namespace xe {
namespace patcher {

PatchDB::PatchDB(const std::filesystem::path patches_root) {
  patches_root_ = patches_root;
  LoadPatches();
}

PatchDB::~PatchDB() {}

void PatchDB::LoadPatches() {
  if (!cvars::apply_patches) {
    return;
  }

  const std::filesystem::path patches_directory = patches_root_ / "patches";
  const std::vector<xe::filesystem::FileInfo> patch_files =
      filesystem::ListFiles(patches_directory);

  for (const xe::filesystem::FileInfo& patch_file : patch_files) {
    // Skip files that doesn't have only title_id as name and .patch as
    // extension
    if (!std::regex_match(path_to_utf8(patch_file.name),
                          patch_filename_regex_)) {
      XELOGE("PatchDB: Skipped loading file {} due to incorrect filename",
             path_to_utf8(patch_file.name));
      continue;
    }

    const PatchFileEntry loaded_title_patches =
        ReadPatchFile(patch_file.path / patch_file.name);
    if (loaded_title_patches.title_id != -1) {
      loaded_patches_.push_back(loaded_title_patches);
    }
  }
  XELOGI("PatchDB: Loaded patches for {} titles", loaded_patches_.size());
}

PatchFileEntry PatchDB::ReadPatchFile(const std::filesystem::path& file_path) {
  PatchFileEntry patch_file;
  std::shared_ptr<cpptoml::table> patch_toml_fields;

  try {
    patch_toml_fields = ParseFile(file_path);
  } catch (...) {
    XELOGE("PatchDB: Cannot load patch file: {}", path_to_utf8(file_path));
    patch_file.title_id = -1;
    return patch_file;
  };

  auto title_name = patch_toml_fields->get_as<std::string>("title_name");
  auto title_id = patch_toml_fields->get_as<std::string>("title_id");

  patch_file.title_id = strtoul((*title_id).c_str(), NULL, 16);
  patch_file.title_name = *title_name;
  ReadHashes(patch_file, patch_toml_fields);

  auto patch_table = patch_toml_fields->get_table_array("patch");

  for (auto patch_table_entry : *patch_table) {
    PatchInfoEntry patch = PatchInfoEntry();
    auto patch_name = *patch_table_entry->get_as<std::string>("name");
    auto patch_desc = *patch_table_entry->get_as<std::string>("desc");
    auto patch_author = *patch_table_entry->get_as<std::string>("author");
    auto is_enabled = *patch_table_entry->get_as<bool>("is_enabled");

    patch.id = 0;  // Todo(Gliniak): Implement id for future GUI stuff
    patch.patch_name = patch_name;
    patch.patch_desc = patch_desc;
    patch.patch_author = patch_author;
    patch.is_enabled = is_enabled;

    // Iterate through all available data sizes
    for (const auto& patch_data_type : patch_data_types_size_) {
      bool success =
          ReadPatchData(patch.patch_data, patch_data_type, patch_table_entry);

      if (!success) {
        XELOGE("PatchDB: Cannot read patch {}", patch_name);
        break;
      }
    }
    patch_file.patch_info.push_back(patch);
  }
  return patch_file;
}

bool PatchDB::ReadPatchData(
    std::vector<PatchDataEntry>& patch_data,
    const std::pair<std::string, PatchData> data_type,
    const std::shared_ptr<cpptoml::table>& patch_table) {
  auto patch_data_tarr = patch_table->get_table_array(data_type.first);
  if (!patch_data_tarr) {
    return true;
  }

  for (const auto& patch_data_table : *patch_data_tarr) {
    uint32_t address = *patch_data_table->get_as<std::uint32_t>("address");
    size_t alloc_size = (size_t)data_type.second.size;

    switch (data_type.second.type) {
      case PatchDataType::kBE8: {
        uint16_t value = *patch_data_table->get_as<uint8_t>("value");
        patch_data.push_back({address, PatchDataValue(alloc_size, value)});
        break;
      }
      case PatchDataType::kBE16: {
        uint16_t value = *patch_data_table->get_as<uint16_t>("value");
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE32: {
        uint32_t value = *patch_data_table->get_as<uint32_t>("value");
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE64: {
        uint64_t value = *patch_data_table->get_as<uint64_t>("value");
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF64: {
        double val = *patch_data_table->get_as<double>("value");
        uint64_t value = *reinterpret_cast<uint64_t*>(&val);
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF32: {
        float value = float(*patch_data_table->get_as<double>("value"));
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kString: {
        std::string value = *patch_data_table->get_as<std::string>("value");
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kU16String: {
        std::u16string value =
            xe::to_utf16(*patch_data_table->get_as<std::string>("value"));
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kByteArray: {
        std::vector<uint8_t> data;
        const std::string value =
            *patch_data_table->get_as<std::string>("value");

        bool success = string_util::hex_string_to_array(data, value);
        if (!success) {
          XELOGW("PatchDB: Cannot convert hex string to byte array! Skipping",
                 address);
          return false;
        }
        patch_data.push_back({address, PatchDataValue(data)});
        break;
      }
      default: {
        XELOGW("PatchDB: Unknown patch data type for address {:08X}! Skipping",
               address);
        return false;
      }
    }
  }
  return true;
}

std::vector<PatchFileEntry> PatchDB::GetTitlePatches(
    const uint32_t title_id, const std::optional<uint64_t> hash) {
  std::vector<PatchFileEntry> title_patches;

  std::copy_if(
      loaded_patches_.cbegin(), loaded_patches_.cend(),
      std::back_inserter(title_patches), [=](const PatchFileEntry entry) {
        bool hash_exist = std::find(entry.hashes.cbegin(), entry.hashes.cend(),
                                    hash) != entry.hashes.cend();

        return entry.title_id == title_id &&
               (entry.hashes.empty() || hash_exist);
      });

  return title_patches;
}

void PatchDB::ReadHashes(PatchFileEntry& patch_entry,
                         std::shared_ptr<cpptoml::table> patch_toml_fields) {
  auto title_hashes = patch_toml_fields->get_array_of<std::string>("hash");

  for (const auto& hash : *title_hashes) {
    patch_entry.hashes.push_back(strtoull(hash.c_str(), NULL, 16));
  }

  auto single_hash = patch_toml_fields->get_as<std::string>("hash");
  if (single_hash) {
    patch_entry.hashes.push_back(strtoull((*single_hash).c_str(), NULL, 16));
  }
}

}  // namespace patcher
}  // namespace xe
