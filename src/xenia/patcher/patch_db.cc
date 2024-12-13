/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <regex>

#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/config.h"
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
             patch_file.name);
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

PatchFileEntry PatchDB::ReadPatchFile(
    const std::filesystem::path& file_path) const {
  PatchFileEntry patch_file;
  toml::parse_result patch_toml_fields;

  try {
    patch_toml_fields = ParseFile(file_path);
  } catch (...) {
    XELOGE("PatchDB: Cannot load patch file: {}", file_path.filename());
    patch_file.title_id = -1;
    return patch_file;
  };

  auto title_name = patch_toml_fields.get_as<std::string>("title_name");
  auto title_id = patch_toml_fields.get_as<std::string>("title_id");
  auto hashes_node = patch_toml_fields.get("hash");

  if (!title_name || !title_id || !hashes_node) {
    XELOGE("PatchDB: Cannot load patch file: {}", file_path.filename());
    patch_file.title_id = -1;
    return patch_file;
  }

  patch_file.title_id = strtoul(title_id->get().c_str(), NULL, 16);
  patch_file.title_name = title_name->get();
  ReadHashes(patch_file, hashes_node);

  auto patch_array = patch_toml_fields.get("patch");
  if (!patch_array->is_array()) {
    return patch_file;
  }

  for (const auto& patch_entry : *patch_array->as_array()) {
    if (!patch_entry.is_table()) {
      continue;
    }

    PatchInfoEntry patch = PatchInfoEntry();
    ReadPatchHeader(patch, patch_entry.as_table());
    patch_file.patch_info.push_back(patch);
  }
  return patch_file;
}

bool PatchDB::ReadPatchData(std::vector<PatchDataEntry>& patch_data,
                            const std::pair<std::string, PatchData> data_type,
                            const toml::table* patch_fields) const {
  auto patch_data_fields = patch_fields->get_as<toml::array>(data_type.first);
  if (!patch_data_fields) {
    return true;
  }

  for (const auto& patch_data_table : *patch_data_fields) {
    if (!patch_data_table.is_table()) {
      continue;
    }

    auto table = patch_data_table.as_table();
    auto address =
        static_cast<uint32_t>(table->get_as<int64_t>("address")->get());
    size_t alloc_size = (size_t)data_type.second.size;

    switch (data_type.second.type) {
      case PatchDataType::kBE8: {
        uint16_t value =
            static_cast<uint16_t>(table->get_as<int64_t>("value")->get());
        patch_data.push_back({address, PatchDataValue(alloc_size, value)});
        break;
      }
      case PatchDataType::kBE16: {
        uint16_t value =
            static_cast<uint16_t>(table->get_as<int64_t>("value")->get());
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE32: {
        uint32_t value =
            static_cast<uint32_t>(table->get_as<int64_t>("value")->get());
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE64: {
        uint64_t value = table->get_as<int64_t>("value")->get();
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF64: {
        const auto value_field = table->get("value");
        double value = 0.0;
        if (value_field->is_floating_point()) {
          value = value_field->as_floating_point()->get();
        }
        if (value_field->is_integer()) {
          value = static_cast<double>(value_field->as_integer()->get());
        }

        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF32: {
        const auto value_field = table->get("value");
        float value = 0.0f;
        if (value_field->is_floating_point()) {
          value = static_cast<float>(value_field->as_floating_point()->get());
        }
        if (value_field->is_integer()) {
          value = static_cast<float>(value_field->as_integer()->get());
        }

        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kString: {
        std::string value = table->get_as<std::string>("value")->get();
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kU16String: {
        std::u16string value =
            xe::to_utf16(table->get_as<std::string>("value")->get());
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kByteArray: {
        std::vector<uint8_t> data;
        const std::string value = table->get_as<std::string>("value")->get();

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

        return entry.title_id == title_id && hash_exist;
      });

  return title_patches;
}

void PatchDB::ReadHashes(PatchFileEntry& patch_entry,
                         const toml::node* hashes_node) const {
  auto add_hash = [&patch_entry](const toml::node* hash_node) {
    if (!hash_node->is_string()) {
      return;
    }

    const auto string_hash = hash_node->as_string()->get();
    if (string_hash.empty()) {
      return;
    }

    patch_entry.hashes.push_back(
        xe::string_util::from_string<uint64_t>(string_hash, true));
  };

  if (hashes_node->is_value()) {
    add_hash(hashes_node);
  }

  if (hashes_node->is_array()) {
    for (const auto& hash_entry : *hashes_node->as_array()) {
      add_hash(&hash_entry);
    }
  }
}

void PatchDB::ReadPatchHeader(PatchInfoEntry& patch_info,
                              const toml::table* patch_fields) const {
  std::string patch_name = {};
  std::string patch_desc = {};
  std::string patch_author = {};
  bool is_enabled = false;

  if (patch_fields->contains("name")) {
    patch_name = patch_fields->get_as<std::string>("name")->get();
  }
  if (patch_fields->contains("desc")) {
    patch_desc = patch_fields->get_as<std::string>("desc")->get();
  }
  if (patch_fields->contains("author")) {
    patch_author = patch_fields->get_as<std::string>("author")->get();
  }
  if (patch_fields->contains("is_enabled")) {
    is_enabled = patch_fields->get_as<bool>("is_enabled")->get();
  }

  patch_info.id = 0;  // Todo(Gliniak): Implement id for future GUI stuff
  patch_info.patch_name = patch_name;
  patch_info.patch_desc = patch_desc;
  patch_info.patch_author = patch_author;
  patch_info.is_enabled = is_enabled;

  // Iterate through all available data sizes
  for (const auto& patch_data_type : patch_data_types_size_) {
    bool success =
        ReadPatchData(patch_info.patch_data, patch_data_type, patch_fields);

    if (!success) {
      XELOGE("PatchDB: Cannot read patch {}", patch_name);
      break;
    }
  }
}

}  // namespace patcher
}  // namespace xe
