/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/xdbf/xdbf_utils.h"

namespace xe {
namespace xdbf {

enum XdbfId : uint64_t {
  kIdTitle = 0x8000,
  kIdXSTC = 0x58535443,
};

enum XdbgMagic : uint32_t {
  kMagicXSTC = 'XSTC',
  kMagicXSTR = 'XSTR',
};

XdbfWrapper::XdbfWrapper() = default;

XBDF_HEADER& XdbfWrapper::get_header() const { return *state_.header; }

XBDF_ENTRY& XdbfWrapper::get_entry(uint32_t n) const {
  return state_.entries[n];
}

XBDF_FILE_LOC& XdbfWrapper::get_file(uint32_t n) const {
  return state_.files[n];
}

bool XdbfWrapper::initialize(uint8_t* buffer, size_t length) {
  if (length <= sizeof(XBDF_HEADER)) {
    return false;
  }

  XdbfState state;

  state.data = buffer;
  state.size = length;

  uint8_t* ptr = state.data;

  state.header = reinterpret_cast<XBDF_HEADER*>(ptr);
  ptr += sizeof(XBDF_HEADER);

  state.entries = reinterpret_cast<XBDF_ENTRY*>(ptr);
  ptr += (sizeof(XBDF_ENTRY) * state.header->entry_count);

  state.files = reinterpret_cast<XBDF_FILE_LOC*>(ptr);
  ptr += (sizeof(XBDF_FILE_LOC) * state.header->free_count);

  state.offset = ptr;

  if (state.header->magic == 'XDBF') {
    state_ = state;
    return true;
  }

  return false;
}

XdbfBlock XdbfWrapper::get_entry(XdbfSection section, uint64_t id) const {
  XdbfBlock block = {nullptr, 0};
  uint32_t x = 0;

  while (x < get_header().entry_used) {
    auto& entry = get_entry(x);

    if (entry.section == section && entry.id == id) {
      block.buffer = state_.offset + entry.offset;
      block.size = entry.size;
      break;
    }

    ++x;
  }

  return block;
}

XdbfBlock get_icon(const XdbfWrapper &ref) {
  return ref.get_entry(kSectionImage, kIdTitle);
}

XdbfLocale get_default_language(const XdbfWrapper &ref) {
  XdbfBlock block = ref.get_entry(kSectionMetadata, kIdXSTC);
  if (block.buffer != nullptr) {
    XDBF_XSTC *xstc = reinterpret_cast<XDBF_XSTC *>(block.buffer);
    assert_true(xstc->magic == kMagicXSTC);

    uint32_t default_language = xstc->default_language;
    return static_cast<XdbfLocale>(default_language);
  }

  return kLocaleEnglish;
}

std::string get_title(const XdbfWrapper &ref) {
  std::string title_str;

  uint64_t language_id = static_cast<uint64_t>(get_default_language(ref));

  XdbfBlock lang_block = ref.get_entry(kSectionStringTable, language_id);

  if (lang_block.buffer != nullptr) {
    XDBF_XSTR_HEADER *xstr_head =
        reinterpret_cast<XDBF_XSTR_HEADER *>(lang_block.buffer);

    assert_true(xstr_head->magic == kMagicXSTR);
    assert_true(xstr_head->version == 1);

    uint16_t str_count = xstr_head->string_count;
    uint8_t *currentAddress = lang_block.buffer + sizeof(XDBF_XSTR_HEADER);

    uint16_t s = 0;
    while (s < str_count && title_str.empty()) {
      XDBF_STRINGTABLE_ENTRY *entry =
          reinterpret_cast<XDBF_STRINGTABLE_ENTRY *>(currentAddress);
      currentAddress += sizeof(XDBF_STRINGTABLE_ENTRY);
      uint16_t len = entry->string_length;

      if (entry->id == static_cast<uint16_t>(kIdTitle)) {
        title_str.resize(static_cast<size_t>(len));
        std::copy(currentAddress, currentAddress + len, title_str.begin());
      }

      ++s;
      currentAddress += len;
    }
  }

  return title_str;
}

}  // namespace xdbf
}  // namespace xe
