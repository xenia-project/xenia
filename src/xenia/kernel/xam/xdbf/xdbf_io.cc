/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xdbf/xdbf_io.h"

namespace xe {
namespace kernel {
namespace xam {

XdbfFile::XdbfFile(const std::span<const uint8_t> buffer) {
  if (buffer.size() <= sizeof(XdbfHeader)) {
    return;
  }

  const uint8_t* ptr = buffer.data();

  if (!LoadHeader(reinterpret_cast<const XdbfHeader*>(ptr))) {
    return;
  }

  ptr += sizeof(XdbfHeader);

  const XdbfFileLoc* free_ptr = reinterpret_cast<const XdbfFileLoc*>(
      ptr + (sizeof(XdbfEntry) * header_.entry_count));

  const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(free_ptr) +
                            (sizeof(XdbfFileLoc) * header_.free_count);

  LoadEntries(reinterpret_cast<const XdbfEntry*>(ptr), data_ptr);
  LoadFreeEntries(free_ptr);
}

bool XdbfFile::LoadHeader(const XdbfHeader* header) {
  if (!header || header->magic != kXdbfSignatureXdbf) {
    return false;
  }

  memcpy(&header_, header, sizeof(XdbfHeader));
  return true;
}

uint32_t XdbfFile::CalculateEntriesSize() const {
  // XDBF always contains at least 1 free entry that marks EOF. That's why we
  // can use it to get size of data in file.
  return free_entries_.back().offset;
}

void XdbfFile::LoadEntries(const XdbfEntry* table_of_content,
                           const uint8_t* data_ptr) {
  if (!table_of_content || !data_ptr) {
    return;
  }

  for (uint32_t i = 0; i < header_.entry_used; i++) {
    entries_.push_back({table_of_content++, data_ptr});
  }
}

void XdbfFile::LoadFreeEntries(const XdbfFileLoc* free_entries) {
  for (uint32_t i = 0; i < header_.free_used; i++) {
    free_entries_.push_back(*free_entries);
    free_entries++;
  }
}

Entry* XdbfFile::GetEntry(uint16_t section, uint64_t id) {
  for (Entry& entry : entries_) {
    if (entry.info.id != id || entry.info.section != section) {
      continue;
    }
    return &entry;
  }
  return nullptr;
}

const Entry* const XdbfFile::GetEntry(uint16_t section, uint64_t id) const {
  for (const Entry& entry : entries_) {
    if (entry.info.id != id || entry.info.section != section) {
      continue;
    }
    return &entry;
  }
  return nullptr;
}

uint32_t XdbfFile::CalculateDataStartOffset() const {
  const uint32_t entry_size = sizeof(XdbfEntry) * header_.entry_count;
  const uint32_t free_size = sizeof(XdbfFileLoc) * header_.free_count;
  return sizeof(XdbfHeader) + entry_size + free_size;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
