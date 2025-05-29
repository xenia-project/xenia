/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_
#define XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_

#include "xenia/kernel/xam/xdbf/xdbf_io.h"

#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.h
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.cpp

enum class SpaSection : uint16_t {
  kMetadata = 0x0001,
  kImage = 0x0002,
  kStringTable = 0x0003,
};

enum class TitleType : uint32_t {
  kSystem = 0,
  kFull = 1,
  kDemo = 2,
  kDownload = 3,
  kUnknown = 4,
  kApp = 5
};

enum class TitleFlags {
  kAlwaysIncludeInProfile = 1,
  kNeverIncludeInProfile = 2,
};

#pragma pack(push, 1)
struct TitleHeaderData {
  xe::be<uint32_t> title_id;
  xe::be<TitleType> title_type;
  xe::be<uint16_t> major;
  xe::be<uint16_t> minor;
  xe::be<uint16_t> build;
  xe::be<uint16_t> revision;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> padding_1;
  xe::be<uint32_t> padding_2;
  xe::be<uint32_t> padding_3;
};
static_assert_size(TitleHeaderData, 32);

struct StatsViewTableEntry {
  xe::be<uint32_t> id;
  xe::be<uint32_t> flags;
  xe::be<uint16_t> shared_index;
  xe::be<uint16_t> string_id;
  xe::be<uint32_t> unused;
};
static_assert_size(StatsViewTableEntry, 0x10);

struct ViewFieldEntry {
  xe::be<uint32_t> size;
  xe::be<uint32_t> property_id;
  xe::be<uint32_t> flags;
  xe::be<uint16_t> attribute_id;
  xe::be<uint16_t> string_id;
  xe::be<uint16_t> aggregation_type;
  xe::be<uint8_t> ordinal;
  xe::be<uint8_t> field_type;
  xe::be<uint32_t> format_type;
  xe::be<uint32_t> unused_1;
  xe::be<uint32_t> unused_2;
};
static_assert_size(ViewFieldEntry, 0x20);

struct SharedViewMetaTableEntry {
  xe::be<uint16_t> column_count;
  xe::be<uint16_t> row_count;
  xe::be<uint32_t> unused_1;
  xe::be<uint32_t> unused_2;
};
static_assert_size(SharedViewMetaTableEntry, 0xC);

struct PropertyBag {
  std::vector<xe::be<uint32_t>> contexts;
  std::vector<xe::be<uint32_t>> properties;
};

struct SharedView {
  std::vector<ViewFieldEntry> column_entries;
  std::vector<ViewFieldEntry> row_entries;
  PropertyBag property_bag;
};

struct ViewTable {
  StatsViewTableEntry view_entry;
  SharedView shared_view;
};

struct AchievementTableEntry {
  xe::be<uint16_t> id;
  xe::be<uint16_t> label_id;
  xe::be<uint16_t> description_id;
  xe::be<uint16_t> unachieved_id;
  xe::be<uint32_t> image_id;
  xe::be<uint16_t> gamerscore;
  xe::be<uint16_t> unkE;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> unk14;
  xe::be<uint32_t> unk18;
  xe::be<uint32_t> unk1C;
  xe::be<uint32_t> unk20;
};
static_assert_size(AchievementTableEntry, 0x24);
#pragma pack(pop)

class SpaInfo : public XdbfFile {
 public:
  SpaInfo(const std::span<uint8_t> buffer);
  ~SpaInfo() = default;

  void Load();

  const uint8_t* ReadXLast(uint32_t& compressed_size,
                           uint32_t& decompressed_size);

  // Checks if provided language exist, if not returns default title language.
  XLanguage GetExistingLanguage(XLanguage language_to_check) const;

  // The game icon image, if found.
  std::span<const uint8_t> title_icon() const;

  std::span<const uint8_t> GetIcon(uint64_t id) const;

  // The game's default language.
  XLanguage default_language() const;

  bool is_system_app() const;
  bool is_demo() const;
  bool include_in_profile() const;

  uint32_t title_id() const;
  // The game's title in its default language.
  std::string title_name() const;

  std::string title_name(XLanguage language) const;

  uint32_t achievement_count() const {
    return static_cast<uint32_t>(achievements_.size());
  }

  const AchievementTableEntry* GetAchievement(uint32_t id);

  std::vector<const AchievementTableEntry*> GetAchievements() const {
    return achievements_;
  }

  std::vector<const XdbfContextTableEntry*> GetContexts() const {
    return contexts_;
  }

  std::vector<const XdbfPropertyTableEntry*> GetProperties() const {
    return properties_;
  }

  const XdbfContextTableEntry* GetContext(uint32_t id);
  const XdbfPropertyTableEntry* GetProperty(uint32_t id);

  uint32_t total_gamerscore() const {
    return std::accumulate(achievements_.cbegin(), achievements_.cend(), 0,
                           [](uint32_t sum, const auto& entry) {
                             return sum + entry->gamerscore;
                           });
  }

  friend bool operator<(const SpaInfo& first, const SpaInfo& second);
  friend bool operator<=(const SpaInfo& first, const SpaInfo& second);
  friend bool operator==(const SpaInfo& first, const SpaInfo& second);

  std::string GetStringTableEntry(XLanguage language, uint16_t string_id) const;

 private:
  // Base info. There should be comparator between different SpaInfos and entry
  // with newer data should replace old one. Such situation can happen when game
  // adds achievements and so on with DLC.
  TitleHeaderData title_header_;

  // SPA is Read-Only so it's reasonable to make it readonly.
  std::vector<const AchievementTableEntry*> achievements_;
  std::vector<const XdbfContextTableEntry*> contexts_;
  std::vector<const XdbfPropertyTableEntry*> properties_;

  typedef std::map<uint16_t, std::string> XdbfLanguageStrings;

  std::map<XLanguage, XdbfLanguageStrings> language_strings_;

  void LoadTitleInformation();
  void LoadAchievements();

  void LoadLanguageData();

  void LoadContexts();
  void LoadProperties();

  template <typename T>
  static T GetSpaEntry(std::vector<T>& container, uint32_t id);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_SPA_INFO_H_
