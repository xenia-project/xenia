/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PATCH_DB_H_
#define XENIA_PATCH_DB_H_

#include <cstring>
#include <map>
#include <optional>
#include <regex>

#include "third_party/cpptoml/include/cpptoml.h"

namespace xe {
namespace patcher {

struct PatchDataValue {
  const size_t alloc_size;
  std::vector<uint8_t> patch_data;

  template <typename T>
  PatchDataValue(const size_t size, const T value) : alloc_size(size) {
    patch_data.resize(alloc_size);
    std::memcpy(patch_data.data(), &value, alloc_size);
  };

  PatchDataValue(const std::vector<uint8_t> value) : alloc_size(value.size()) {
    patch_data.resize(alloc_size);
    std::memcpy(patch_data.data(), value.data(), alloc_size);
  };

  PatchDataValue(const std::string value) : alloc_size(value.size()) {
    patch_data.resize(alloc_size);
    std::memcpy(patch_data.data(), value.c_str(), alloc_size);
  };

  PatchDataValue(const std::u16string value) : alloc_size(value.size() * 2) {
    patch_data.resize(alloc_size);
    std::memcpy(patch_data.data(), value.c_str(), alloc_size);
  };
};

struct PatchDataEntry {
  const uint32_t address;
  const PatchDataValue data;

  PatchDataEntry(const uint32_t memory_address, const PatchDataValue patch_data)
      : address(memory_address), data(patch_data){};
};

struct PatchInfoEntry {
  uint32_t id;
  std::string patch_name;
  std::string patch_desc;
  std::string patch_author;
  std::vector<PatchDataEntry> patch_data;
  bool is_enabled;
};

struct PatchFileEntry {
  uint32_t title_id;
  std::string title_name;
  std::vector<uint64_t> hashes;
  std::vector<PatchInfoEntry> patch_info;
};

enum class PatchDataType {
  kBE8,
  kBE16,
  kBE32,
  kBE64,
  kF32,
  kF64,
  kString,
  kU16String,
  kByteArray
};

struct PatchData {
  uint8_t size;
  PatchDataType type;

  PatchData(uint8_t size_, PatchDataType type_) : size(size_), type(type_){};
};

class PatchDB {
 public:
  PatchDB(const std::filesystem::path patches_root);
  ~PatchDB();

  void LoadPatches();

  PatchFileEntry ReadPatchFile(const std::filesystem::path& file_path);
  bool ReadPatchData(std::vector<PatchDataEntry>& patch_data,
                     const std::pair<std::string, PatchData> data_type,
                     const std::shared_ptr<cpptoml::table>& patch_table);

  std::vector<PatchFileEntry> GetTitlePatches(
      const uint32_t title_id, const std::optional<uint64_t> hash);
  std::vector<PatchFileEntry>& GetAllPatches() { return loaded_patches_; }

 private:
  void ReadHashes(PatchFileEntry& patch_entry,
                  std::shared_ptr<cpptoml::table> patch_toml_fields);

  inline static const std::regex patch_filename_regex_ =
      std::regex("^[A-Fa-f0-9]{8}.*\\.patch\\.toml$");

  const std::map<std::string, PatchData> patch_data_types_size_ = {
      {"string", PatchData(0, PatchDataType::kString)},
      {"u16string", PatchData(0, PatchDataType::kU16String)},
      {"array", PatchData(0, PatchDataType::kByteArray)},
      {"f64", PatchData(sizeof(uint64_t), PatchDataType::kF64)},
      {"f32", PatchData(sizeof(uint32_t), PatchDataType::kF32)},
      {"be64", PatchData(sizeof(uint64_t), PatchDataType::kBE64)},
      {"be32", PatchData(sizeof(uint32_t), PatchDataType::kBE32)},
      {"be16", PatchData(sizeof(uint16_t), PatchDataType::kBE16)},
      {"be8", PatchData(sizeof(uint8_t), PatchDataType::kBE8)}};

  std::vector<PatchFileEntry> loaded_patches_;
  std::filesystem::path patches_root_;
};
}  // namespace patcher
}  // namespace xe

#endif  // XENIA_PATCH_LOADER_H_
