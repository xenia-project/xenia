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
struct PatchDataEntry {
  const uint8_t alloc_size_;
  const uint32_t memory_address_;
  const uint64_t new_value_;

  PatchDataEntry(const uint8_t alloc_size, const uint32_t memory_address,
                 const uint64_t new_value)
      : alloc_size_(alloc_size),
        memory_address_(memory_address),
        new_value_(new_value){};
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

class PatchDB {
 public:
  PatchDB(const std::filesystem::path patches_root);
  ~PatchDB();

  void LoadPatches();

  PatchFileEntry ReadPatchFile(const std::filesystem::path& file_path);
  std::vector<PatchDataEntry>* ReadPatchData(
      const std::string size_type,
      const std::shared_ptr<cpptoml::table>& patch_table);

  std::vector<PatchFileEntry> GetTitlePatches(uint32_t title_id,
                                              const uint64_t hash);
  std::vector<PatchFileEntry>& GetAllPatches() { return loaded_patches; }

 private:
  std::vector<PatchFileEntry> loaded_patches;
  std::filesystem::path patches_root_;

  uint8_t GetAllocSize(const std::string provided_size);

  const std::map<std::string, size_t> patch_data_types_size = {
      {"f64", sizeof(uint64_t)},  {"f32", sizeof(uint32_t)},
      {"be64", sizeof(uint64_t)}, {"be32", sizeof(uint32_t)},
      {"be16", sizeof(uint16_t)}, {"be8", sizeof(uint8_t)}};
};

}  // namespace patcher
}  // namespace xe

#endif  // XENIA_PATCH_LOADER_H_
