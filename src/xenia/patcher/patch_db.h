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

#include <map>

#include "third_party/cpptoml/include/cpptoml.h"

namespace xe {
namespace patcher {

struct PatchDataValue {
  const size_t alloc_size_;
  const uint8_t* patch_data_ptr_;

  PatchDataValue(const size_t alloc_size, const uint64_t value)
      : alloc_size_(alloc_size) {
    patch_data_ptr_ = new uint8_t[alloc_size_];
    memcpy((void*)patch_data_ptr_, &value, alloc_size);
  };

  PatchDataValue(const size_t alloc_size, const float value)
      : alloc_size_(alloc_size) {
    patch_data_ptr_ = new uint8_t[alloc_size_];
    memcpy((void*)patch_data_ptr_, &value, alloc_size);
  };

  PatchDataValue(const size_t alloc_size, const std::vector<uint8_t> value)
      : alloc_size_(alloc_size) {
    patch_data_ptr_ = new uint8_t[alloc_size_];
    memcpy((void*)patch_data_ptr_, value.data(), alloc_size);
  };

  PatchDataValue(const std::string value) : alloc_size_(value.size()) {
    patch_data_ptr_ = new uint8_t[alloc_size_];
    memcpy((void*)patch_data_ptr_, value.c_str(), alloc_size_);
  };

  PatchDataValue(const std::u16string value) : alloc_size_(value.size() * 2) {
    patch_data_ptr_ = new uint8_t[alloc_size_];
    memcpy((void*)patch_data_ptr_, value.c_str(), alloc_size_);
  };
};

struct PatchDataEntry {
  const uint32_t memory_address_;
  const PatchDataValue new_data_;

  PatchDataEntry(const uint32_t memory_address, const PatchDataValue new_data)
      : memory_address_(memory_address), new_data_(new_data){};
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
  uint64_t hash = 0;
  std::vector<PatchInfoEntry> patch_info;
};

enum class PatchDataType {
  be8,
  be16,
  be32,
  be64,
  f32,
  f64,
  string,
  u16string,
  byte_array
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

  std::vector<PatchFileEntry> GetTitlePatches(uint32_t title_id,
                                              const uint64_t hash);
  std::vector<PatchFileEntry>& GetAllPatches() { return loaded_patches; }

 private:
  std::vector<PatchFileEntry> loaded_patches;
  std::filesystem::path patches_root_;

  const std::map<std::string, PatchData> patch_data_types_size = {
      {"string", PatchData(0, PatchDataType::string)},
      {"u16string", PatchData(0, PatchDataType::u16string)},
      {"array", PatchData(0, PatchDataType::byte_array)},
      {"f64", PatchData(sizeof(uint64_t), PatchDataType::f64)},
      {"f32", PatchData(sizeof(uint32_t), PatchDataType::f32)},
      {"be64", PatchData(sizeof(uint64_t), PatchDataType::be64)},
      {"be32", PatchData(sizeof(uint32_t), PatchDataType::be32)},
      {"be16", PatchData(sizeof(uint16_t), PatchDataType::be16)},
      {"be8", PatchData(sizeof(uint8_t), PatchDataType::be8)}};
};

}  // namespace patcher
}  // namespace xe

#endif  // XENIA_PATCH_LOADER_H_
