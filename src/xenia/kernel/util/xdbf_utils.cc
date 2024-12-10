/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xdbf_utils.h"
#include <map>

namespace xe {
namespace kernel {
namespace util {

constexpr fourcc_t kXdbfSignatureXdbf = make_fourcc("XDBF");
constexpr fourcc_t kXdbfSignatureXstc = make_fourcc("XSTC");
constexpr fourcc_t kXdbfSignatureXstr = make_fourcc("XSTR");
constexpr fourcc_t kXdbfSignatureXach = make_fourcc("XACH");
constexpr fourcc_t kXdbfSignatureXprp = make_fourcc("XPRP");
constexpr fourcc_t kXdbfSignatureXcxt = make_fourcc("XCXT");
constexpr fourcc_t kXdbfSignatureXvc2 = make_fourcc("XVC2");
constexpr fourcc_t kXdbfSignatureXmat = make_fourcc("XMAT");
constexpr fourcc_t kXdbfSignatureXsrc = make_fourcc("XSRC");
constexpr fourcc_t kXdbfSignatureXthd = make_fourcc("XTHD");

constexpr uint64_t kXdbfIdTitle = 0x8000;
constexpr uint64_t kXdbfIdXstc = 0x58535443;
constexpr uint64_t kXdbfIdXach = 0x58414348;
constexpr uint64_t kXdbfIdXprp = 0x58505250;
constexpr uint64_t kXdbfIdXctx = 0x58435854;
constexpr uint64_t kXdbfIdXvc2 = 0x58564332;
constexpr uint64_t kXdbfIdXmat = 0x584D4154;
constexpr uint64_t kXdbfIdXsrc = 0x58535243;
constexpr uint64_t kXdbfIdXthd = 0x58544844;

XdbfWrapper::XdbfWrapper(const uint8_t* data, size_t data_size)
    : data_(data), data_size_(data_size) {
  if (!data || data_size <= sizeof(XbdfHeader)) {
    data_ = nullptr;
    return;
  }

  const uint8_t* ptr = data_;

  header_ = reinterpret_cast<const XbdfHeader*>(ptr);
  ptr += sizeof(XbdfHeader);
  if (header_->magic != kXdbfSignatureXdbf) {
    data_ = nullptr;
    return;
  }

  entries_ = reinterpret_cast<const XbdfEntry*>(ptr);
  ptr += sizeof(XbdfEntry) * header_->entry_count;

  files_ = reinterpret_cast<const XbdfFileLoc*>(ptr);
  ptr += sizeof(XbdfFileLoc) * header_->free_count;

  content_offset_ = ptr;
}

XdbfBlock XdbfWrapper::GetEntry(XdbfSection section, uint64_t id) const {
  for (uint32_t i = 0; i < header_->entry_used; ++i) {
    auto& entry = entries_[i];
    if (entry.section == static_cast<uint16_t>(section) && entry.id == id) {
      XdbfBlock block;
      block.buffer = content_offset_ + entry.offset;
      block.size = entry.size;
      return block;
    }
  }
  return {0};
}

std::string XdbfWrapper::GetStringTableEntry(XLanguage language,
                                             uint16_t string_id) const {
  auto language_block =
      GetEntry(XdbfSection::kStringTable, static_cast<uint64_t>(language));
  if (!language_block) {
    return "";
  }

  auto xstr_head =
      reinterpret_cast<const XdbfSectionHeader*>(language_block.buffer);
  assert_true(xstr_head->magic == kXdbfSignatureXstr);
  assert_true(xstr_head->version == 1);

  const uint8_t* ptr = language_block.buffer + sizeof(XdbfSectionHeader);
  const uint16_t string_count = xe::byte_swap<uint16_t>(*(uint16_t*)ptr);
  ptr += sizeof(uint16_t);

  for (uint16_t i = 0; i < string_count; ++i) {
    auto entry = reinterpret_cast<const XdbfStringTableEntry*>(ptr);
    ptr += sizeof(XdbfStringTableEntry);
    if (entry->id == string_id) {
      return std::string(reinterpret_cast<const char*>(ptr),
                         entry->string_length);
    }
    ptr += entry->string_length;
  }
  return "";
}

std::vector<XdbfAchievementTableEntry> XdbfWrapper::GetAchievements() const {
  std::vector<XdbfAchievementTableEntry> achievements;

  auto achievement_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXach);
  if (!achievement_table) {
    return achievements;
  }

  auto xach_head =
      reinterpret_cast<const XdbfSectionHeader*>(achievement_table.buffer);
  assert_true(xach_head->magic == kXdbfSignatureXach);
  assert_true(xach_head->version == 1);

  const uint8_t* ptr = achievement_table.buffer + sizeof(XdbfSectionHeader);
  const uint16_t achievement_count = xe::byte_swap<uint16_t>(*(uint16_t*)ptr);
  ptr += sizeof(uint16_t);

  for (uint16_t i = 0; i < achievement_count; ++i) {
    auto entry = reinterpret_cast<const XdbfAchievementTableEntry*>(ptr);
    ptr += sizeof(XdbfAchievementTableEntry);
    achievements.push_back(*entry);
  }
  return achievements;
}

std::vector<XdbfPropertyTableEntry> XdbfWrapper::GetProperties() const {
  std::vector<XdbfPropertyTableEntry> properties;

  auto property_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXprp);
  if (!property_table) {
    return properties;
  }

  auto xprp_head =
      reinterpret_cast<const XdbfSectionHeader*>(property_table.buffer);
  assert_true(xprp_head->magic == kXdbfSignatureXprp);
  assert_true(xprp_head->version == 1);

  const uint8_t* ptr = property_table.buffer + sizeof(XdbfSectionHeader);
  const uint16_t properties_count = xe::byte_swap<uint16_t>(*(uint16_t*)ptr);
  ptr += sizeof(uint16_t);

  for (uint16_t i = 0; i < properties_count; i++) {
    auto entry = reinterpret_cast<const XdbfPropertyTableEntry*>(ptr);
    ptr += sizeof(XdbfPropertyTableEntry);
    properties.push_back(*entry);
  }
  return properties;
}

std::vector<XdbfContextTableEntry> XdbfWrapper::GetContexts() const {
  std::vector<XdbfContextTableEntry> contexts;

  auto contexts_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXctx);
  if (!contexts_table) {
    return contexts;
  }

  auto xcxt_head =
      reinterpret_cast<const XdbfSectionHeader*>(contexts_table.buffer);
  assert_true(xcxt_head->magic == kXdbfSignatureXcxt);
  assert_true(xcxt_head->version == 1);

  const uint8_t* ptr = contexts_table.buffer + sizeof(XdbfSectionHeader);
  const uint32_t contexts_count = xe::byte_swap<uint32_t>(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t);

  for (uint32_t i = 0; i < contexts_count; i++) {
    auto entry = reinterpret_cast<const XdbfContextTableEntry*>(ptr);
    ptr += sizeof(XdbfContextTableEntry);
    contexts.push_back(*entry);
  }
  return contexts;
}

std::vector<XdbfViewTable> XdbfWrapper::GetStatsView() const {
  std::vector<XdbfViewTable> entries;

  auto stats_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXvc2);
  if (!stats_table) {
    return entries;
  }

  auto xvc2_head =
      reinterpret_cast<const XdbfSectionHeader*>(stats_table.buffer);
  assert_true(xvc2_head->magic == kXdbfSignatureXvc2);
  assert_true(xvc2_head->version == 1);

  const uint8_t* ptr = stats_table.buffer + sizeof(XdbfSectionHeader);
  const uint16_t shared_view_count = xe::byte_swap<uint16_t>(*(uint16_t*)ptr);
  ptr += sizeof(uint16_t);

  std::map<uint16_t, XdbfSharedView> shared_view_entries;
  for (uint16_t i = 0; i < shared_view_count; i++) {
    uint32_t byte_count = 0;
    shared_view_entries.emplace(i, GetSharedView(ptr, byte_count));
    ptr += byte_count;
  }

  const uint16_t views_count = xe::byte_swap(*(uint16_t*)ptr);
  ptr += sizeof(uint16_t);

  for (uint16_t i = 0; i < views_count; i++) {
    auto stat = reinterpret_cast<const XdbfStatsViewTableEntry*>(
        ptr + i * sizeof(XdbfStatsViewTableEntry));

    XdbfViewTable table;
    table.view_entry = *stat;
    table.shared_view = shared_view_entries[stat->shared_index];
    entries.push_back(table);
  }

  return entries;
}

const uint8_t* XdbfWrapper::ReadXLast(uint32_t& compressed_size,
                                      uint32_t& decompressed_size) const {
  auto xlast_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXsrc);
  if (!xlast_table) {
    return nullptr;
  }

  auto xlast_head =
      reinterpret_cast<const XdbfSectionHeader*>(xlast_table.buffer);
  assert_true(xlast_head->magic == kXdbfSignatureXsrc);
  assert_true(xlast_head->version == 1);

  const uint8_t* ptr = xlast_table.buffer + sizeof(XdbfSectionHeader);

  const uint32_t filename_length = xe::byte_swap(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t) + filename_length;

  decompressed_size = xe::byte_swap(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t);

  compressed_size = xe::byte_swap(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t);

  return ptr;
}

XdbfTitleHeaderData XdbfWrapper::GetTitleInformation() const {
  auto xlast_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXthd);
  if (!xlast_table) {
    return {};
  }

  auto xlast_head =
      reinterpret_cast<const XdbfSectionHeader*>(xlast_table.buffer);
  assert_true(xlast_head->magic == kXdbfSignatureXthd);
  assert_true(xlast_head->version == 1);

  const XdbfTitleHeaderData* ptr = reinterpret_cast<const XdbfTitleHeaderData*>(
      xlast_table.buffer + sizeof(XdbfSectionHeader));

  return *ptr;
}

XdbfAchievementTableEntry XdbfWrapper::GetAchievement(const uint32_t id) const {
  const auto achievements = GetAchievements();

  for (const auto& entry : achievements) {
    if (entry.id != id) {
      continue;
    }
    return entry;
  }
  return {};
}

XdbfPropertyTableEntry XdbfWrapper::GetProperty(const uint32_t id) const {
  const auto properties = GetProperties();

  for (const auto& entry : properties) {
    if (entry.id != id) {
      continue;
    }
    return entry;
  }
  return {};
}

XdbfContextTableEntry XdbfWrapper::GetContext(const uint32_t id) const {
  const auto contexts = GetContexts();

  for (const auto& entry : contexts) {
    if (entry.id != id) {
      continue;
    }
    return entry;
  }
  return {};
}

XdbfSharedView XdbfWrapper::GetSharedView(const uint8_t* ptr,
                                          uint32_t& byte_count) const {
  XdbfSharedView shared_view;

  byte_count += sizeof(XdbfSharedViewMetaTableEntry);
  auto table_header =
      reinterpret_cast<const XdbfSharedViewMetaTableEntry*>(ptr);
  ptr += sizeof(XdbfSharedViewMetaTableEntry);

  for (uint16_t i = 0; i < table_header->column_count - 1; i++) {
    auto view_field = reinterpret_cast<const XdbfViewFieldEntry*>(
        ptr + (i * sizeof(XdbfViewFieldEntry)));
    shared_view.column_entries.push_back(*view_field);
  }

  // Move pointer forward to next data
  ptr += (table_header->column_count * sizeof(XdbfViewFieldEntry));
  byte_count += (table_header->column_count * sizeof(XdbfViewFieldEntry));

  for (uint16_t i = 0; i < table_header->row_count - 1; i++) {
    auto view_field = reinterpret_cast<const XdbfViewFieldEntry*>(
        ptr + (i * sizeof(XdbfViewFieldEntry)));
    shared_view.row_entries.push_back(*view_field);
  }

  ptr += (table_header->row_count * sizeof(XdbfViewFieldEntry));
  byte_count += (table_header->row_count * sizeof(XdbfViewFieldEntry));

  std::vector<xe::be<uint32_t>> contexts, properties;
  GetPropertyBagMetadata(ptr, byte_count, contexts, properties);

  shared_view.property_bag.contexts = contexts;
  shared_view.property_bag.properties = properties;

  return shared_view;
}

void XdbfWrapper::GetPropertyBagMetadata(
    const uint8_t* ptr, uint32_t& byte_count,
    std::vector<xe::be<uint32_t>>& contexts,
    std::vector<xe::be<uint32_t>>& properties) const {
  auto xpbm_header = reinterpret_cast<const XdbfSectionHeader*>(ptr);
  ptr += sizeof(XdbfSectionHeader);

  byte_count += sizeof(XdbfSectionHeader) + 2 * sizeof(uint32_t);

  uint32_t context_count = xe::byte_swap<uint32_t>(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t);

  uint32_t properties_count = xe::byte_swap<uint32_t>(*(uint32_t*)ptr);
  ptr += sizeof(uint32_t);

  contexts = std::vector<xe::be<uint32_t>>(context_count);
  std::memcpy(contexts.data(), ptr, context_count * sizeof(uint32_t));

  ptr += context_count * sizeof(uint32_t);

  properties = std::vector<xe::be<uint32_t>>(properties_count);
  std::memcpy(properties.data(), ptr, sizeof(uint32_t) * properties_count);

  byte_count += (context_count + properties_count) * sizeof(uint32_t);
}

XdbfPropertyBag XdbfWrapper::GetMatchCollection() const {
  XdbfPropertyBag property_bag;

  auto stats_table = GetEntry(XdbfSection::kMetadata, kXdbfIdXmat);
  if (!stats_table) {
    return property_bag;
  }

  auto xvc2_head =
      reinterpret_cast<const XdbfSectionHeader*>(stats_table.buffer);
  assert_true(xvc2_head->magic == kXdbfSignatureXmat);
  assert_true(xvc2_head->version == 1);

  const uint8_t* ptr = stats_table.buffer + sizeof(XdbfSectionHeader);

  std::vector<xe::be<uint32_t>> contexts, properties;
  uint32_t byte_count = 0;

  GetPropertyBagMetadata(ptr, byte_count, contexts, properties);

  property_bag.contexts = contexts;
  property_bag.properties = properties;

  return property_bag;
}

XLanguage XdbfGameData::GetExistingLanguage(XLanguage language_to_check) const {
  // A bit of a hack. Check if title in specific language exist.
  // If it doesn't then for sure language is not supported.
  return title(language_to_check).empty() ? default_language()
                                          : language_to_check;
}

XdbfBlock XdbfGameData::icon() const {
  return GetEntry(XdbfSection::kImage, kXdbfIdTitle);
}

XLanguage XdbfGameData::default_language() const {
  auto block = GetEntry(XdbfSection::kMetadata, kXdbfIdXstc);
  if (!block.buffer) {
    return XLanguage::kEnglish;
  }
  auto xstc = reinterpret_cast<const XdbfXstc*>(block.buffer);
  assert_true(xstc->magic == kXdbfSignatureXstc);
  return static_cast<XLanguage>(static_cast<uint32_t>(xstc->default_language));
}

std::string XdbfGameData::title() const {
  return GetStringTableEntry(default_language(), kXdbfIdTitle);
}

std::string XdbfGameData::title(XLanguage language) const {
  return GetStringTableEntry(language, kXdbfIdTitle);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
