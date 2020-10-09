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
#include "xenia/memory.h"

#include "xenia/patcher/patch_db.h"

DEFINE_bool(apply_patches, true, "Enables patching functionality", "General");

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
  const std::vector<xe::filesystem::FileInfo>& patch_files =
      filesystem::ListFiles(patches_directory);
  const std::regex file_name_regex_match =
      std::regex("^[A-Fa-f0-9]{8}.*\\.patch$");

  for (const xe::filesystem::FileInfo& patch_file : patch_files) {
    // Skip files that doesn't have only title_id as name and .patch as
    // extension
    if (!std::regex_match(path_to_utf8(patch_file.name),
                          file_name_regex_match)) {
      XELOGE("PatchDB: Skipped loading file {} due to incorrect filename",
             path_to_utf8(patch_file.name));
      continue;
    }

    const PatchFileEntry loaded_title_patches =
        ReadPatchFile(patch_file.path / patch_file.name);
    if (loaded_title_patches.title_id != -1) {
      loaded_patches.push_back(loaded_title_patches);
    }
  }
  XELOGI("PatchDB: Loaded patches for {} titles", loaded_patches.size());
}

PatchFileEntry PatchDB::ReadPatchFile(const std::filesystem::path& file_path) {
  PatchFileEntry patchFile;
  std::shared_ptr<cpptoml::table> patch_toml_fields;

  try {
    patch_toml_fields = cpptoml::parse_file(path_to_utf8(file_path));
  } catch (...) {
    XELOGE("PatchDB: Cannot load patch file: {}", path_to_utf8(file_path));
    patchFile.title_id = -1;
    return patchFile;
  };

  auto title_name = patch_toml_fields->get_as<std::string>("title_name");
  auto title_id = patch_toml_fields->get_as<std::string>("title_id");
  auto title_hash = patch_toml_fields->get_as<std::string>("hash");

  patchFile.title_id = strtoul((*title_id).c_str(), NULL, 16);
  patchFile.hash = strtoull((*title_hash).c_str(), NULL, 16);
  patchFile.title_name = *title_name;

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
    for (const auto& type : patch_data_types_size) {
      auto patch_entries = ReadPatchData(type.first, patch_table_entry);

      for (const PatchDataEntry& patch_entry : *patch_entries) {
        patch.patch_data.push_back(patch_entry);
      }
    }
    patchFile.patch_info.push_back(patch);
  }
  return patchFile;
}

std::vector<PatchDataEntry>* PatchDB::ReadPatchData(
    const std::string size_type,
    const std::shared_ptr<cpptoml::table>& patch_table) {
  std::vector<PatchDataEntry>* patch_data = new std::vector<PatchDataEntry>();
  auto patch_data_tarr = patch_table->get_table_array(size_type);

  if (!patch_data_tarr) {
    return patch_data;
  }

  for (const auto& patch_data_table : *patch_data_tarr) {
    uint32_t address = *patch_data_table->get_as<std::uint32_t>("address");
    uint64_t value = *patch_data_table->get_as<std::uint64_t>("value");

    if (size_type == "f32") {
      float fvalue = float(*patch_data_table->get_as<double>("value"));
      value = *(reinterpret_cast<uint32_t*>(&fvalue));
    } else if (size_type == "f64") {
      double dvalue = *patch_data_table->get_as<double>("value");
      value = *(reinterpret_cast<uint64_t*>(&dvalue));
    }

    PatchDataEntry patchData = {GetAllocSize(size_type), address, value};
    patch_data->push_back(patchData);
  }
  return patch_data;
}

std::vector<PatchFileEntry> PatchDB::GetTitlePatches(uint32_t title_id,
                                                     const uint64_t hash) {
  std::vector<PatchFileEntry> title_patches;

  std::copy_if(loaded_patches.cbegin(), loaded_patches.cend(),
               std::back_inserter(title_patches),
               [=](const PatchFileEntry entry) {
                 return entry.title_id == title_id &&
                        (!entry.hash || entry.hash == hash);
               });

  return title_patches;
}

uint8_t PatchDB::GetAllocSize(const std::string provided_size) {
  uint8_t alloc_size = 0;

  auto itr = patch_data_types_size.find(provided_size);
  if (itr == patch_data_types_size.cend()) {
    XELOGW("PatchDB: Unsupported patch type!");
    return alloc_size;
  }

  alloc_size = uint8_t(itr->second);
  return alloc_size;
}

}  // namespace patcher
}  // namespace xe
