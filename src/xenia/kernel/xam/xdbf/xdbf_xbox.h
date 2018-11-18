/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XDBF_XDBF_XBOX_H_
#define XENIA_KERNEL_XAM_XDBF_XDBF_XBOX_H_

namespace xe {
namespace kernel {
namespace xam {
namespace xdbf {

/* Native XDBF structs used by 360 are in this file */

struct XdbfStringTableEntry {
  xe::be<uint16_t> id;
  xe::be<uint16_t> string_length;
};
static_assert_size(XdbfStringTableEntry, 4);

#pragma pack(push, 1)
struct X_XDBF_HEADER {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> entry_count;
  xe::be<uint32_t> entry_used;
  xe::be<uint32_t> free_count;
  xe::be<uint32_t> free_used;
};
static_assert_size(X_XDBF_HEADER, 24);

struct X_XDBF_ENTRY {
  xe::be<uint16_t> section;
  xe::be<uint64_t> id;
  xe::be<uint32_t> offset;
  xe::be<uint32_t> size;
};
static_assert_size(X_XDBF_ENTRY, 18);

struct X_XDBF_FILELOC {
  xe::be<uint32_t> offset;
  xe::be<uint32_t> size;
};
static_assert_size(X_XDBF_FILELOC, 8);

struct X_XDBF_XSTC_DATA {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> size;
  xe::be<uint32_t> default_language;
};
static_assert_size(X_XDBF_XSTC_DATA, 16);

struct X_XDBF_XTHD_DATA {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> unk8;
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> unk10;  // always 1?
  xe::be<uint16_t> title_version_major;
  xe::be<uint16_t> title_version_minor;
  xe::be<uint16_t> title_version_build;
  xe::be<uint16_t> title_version_revision;
  xe::be<uint32_t> unk1C;
  xe::be<uint32_t> unk20;
  xe::be<uint32_t> unk24;
  xe::be<uint32_t> unk28;
};
static_assert_size(X_XDBF_XTHD_DATA, 0x2C);

struct X_XDBF_TABLE_HEADER {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> version;
  xe::be<uint32_t> size;
  xe::be<uint16_t> count;
};
static_assert_size(X_XDBF_TABLE_HEADER, 14);

struct X_XDBF_SPA_ACHIEVEMENT {
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
static_assert_size(X_XDBF_SPA_ACHIEVEMENT, 0x24);

struct X_XDBF_GPD_ACHIEVEMENT {
  xe::be<uint32_t> magic;
  xe::be<uint32_t> id;
  xe::be<uint32_t> image_id;
  xe::be<uint32_t> gamerscore;
  xe::be<uint32_t> flags;
  xe::be<uint64_t> unlock_time;
  // wchar_t* title;
  // wchar_t* description;
  // wchar_t* unlocked_description;
};

// from https://github.com/xemio/testdev/blob/master/xkelib/xam/_xamext.h
struct X_XDBF_GPD_TITLEPLAYED {
  xe::be<uint32_t> title_id;
  xe::be<uint32_t> achievements_possible;
  xe::be<uint32_t> achievements_earned;
  xe::be<uint32_t> gamerscore_total;
  xe::be<uint32_t> gamerscore_earned;
  xe::be<uint16_t> reserved_achievement_count;

  // the following are meant to be split into possible/earned, 1 byte each
  // but who cares
  xe::be<uint16_t> all_avatar_awards;
  xe::be<uint16_t> male_avatar_awards;
  xe::be<uint16_t> female_avatar_awards;
  xe::be<uint32_t> reserved_flags;
  xe::be<uint64_t> last_played;
  // wchar_t* title_name;
};
#pragma pack(pop)

}  // namespace xdbf
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XDBF_XDBF_XBOX_H_