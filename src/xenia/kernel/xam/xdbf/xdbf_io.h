/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_XDBF_IO_H_
#define XENIA_KERNEL_XAM_XDBF_XDBF_IO_H_

#include <span>
#include <string>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.h
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.cpp

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

#pragma pack(push, 1)
struct XdbfHeader {
  XdbfHeader() {
    magic = kXdbfSignatureXdbf;
    version = 0x10000;
    entry_count = 0;
    entry_used = 0;
    free_count = 0;
    free_used = 0;
  }

  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> entry_count;
  xe::be<uint32_t> entry_used;
  xe::be<uint32_t> free_count;
  xe::be<uint32_t> free_used;
};
static_assert_size(XdbfHeader, 24);

struct XdbfEntry {
  xe::be<uint16_t> section;
  xe::be<uint64_t> id;
  xe::be<uint32_t> offset;
  xe::be<uint32_t> size;
};
static_assert_size(XdbfEntry, 18);

struct XdbfFileLoc {
  xe::be<uint32_t> offset;
  xe::be<uint32_t> size;
};
static_assert_size(XdbfFileLoc, 8);

struct XdbfXstc {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> size;
  xe::be<uint32_t> default_language;
};
static_assert_size(XdbfXstc, 16);

struct XdbfSectionHeader {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> size;
};
static_assert_size(XdbfSectionHeader, 12);

struct XdbfSectionHeaderEx {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> size;
  xe::be<uint16_t> count;
};
static_assert_size(XdbfSectionHeaderEx, 14);

struct XdbfStringTableEntry {
  xe::be<uint16_t> id;
  xe::be<uint16_t> string_length;
};
static_assert_size(XdbfStringTableEntry, 4);

struct XdbfContextTableEntry {
  xe::be<uint32_t> id;
  xe::be<uint16_t> unk1;
  xe::be<uint16_t> string_id;
  xe::be<uint32_t> max_value;
  xe::be<uint32_t> default_value;
};
static_assert_size(XdbfContextTableEntry, 16);

struct XdbfPropertyTableEntry {
  xe::be<uint32_t> id;
  xe::be<uint16_t> string_id;
  xe::be<uint16_t> data_size;
};
static_assert_size(XdbfPropertyTableEntry, 8);
#pragma pack(pop)

struct XdbfBlock {
  const uint8_t* buffer;
  size_t size;

  operator bool() const { return buffer != nullptr; }
};

struct Entry {
  Entry() {
    info.id = 0;
    info.offset = 0;
    info.section = 0;
    info.size = 0;
  }

  // Offset must be filled externally!
  Entry(const uint64_t id, const uint16_t section, const uint32_t size) {
    info.id = id;
    info.section = section;
    info.size = size;

    data.resize(size);
  }

  Entry(const XdbfEntry* entry, const uint8_t* entry_data) {
    info = *entry;
    data.resize(info.size);
    memcpy(data.data(), entry_data + info.offset, info.size);
  }

  XdbfEntry info;
  std::vector<uint8_t> data;
};

// Wraps an XDBF (XboxDataBaseFormat) in-memory database.
// https://free60project.github.io/wiki/XDBF.html
class XdbfFile {
 public:
  XdbfFile() = default;
  XdbfFile(const std::span<const uint8_t> buffer);

  ~XdbfFile() = default;

  const Entry* const GetEntry(uint16_t section, uint64_t id) const;

 protected:
  XdbfHeader header_ = {};
  std::vector<Entry> entries_ = {};
  std::vector<XdbfFileLoc> free_entries_ = {};

  // Gets an entry in the given section.
  // If the entry is not found the returned block will be nullptr.
  Entry* GetEntry(uint16_t section, uint64_t id);
  uint32_t CalculateDataStartOffset() const;
  uint32_t CalculateEntriesSize() const;

 private:
  bool LoadHeader(const XdbfHeader* header);
  void LoadEntries(const XdbfEntry* table_of_content, const uint8_t* data_ptr);
  void LoadFreeEntries(const XdbfFileLoc* free_entries);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_XDBF_IO_H_
