/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XDBF_UTILS_H_
#define XENIA_KERNEL_UTIL_XDBF_UTILS_H_

#include <string>
#include <vector>

#include "xenia/base/memory.h"

namespace xe {
namespace kernel {
namespace util {

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.h
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.cpp

enum class XdbfSpaID : uint64_t {
  Xach = 'XACH',
  Xstr = 'XSTR',
  Xstc = 'XSTC',
  Title = 0x8000,
};

enum class XdbfSpaSection : uint16_t {
  kMetadata = 0x1,
  kImage = 0x2,
  kStringTable = 0x3,
};

enum class XdbfGpdSection : uint16_t {
  kAchievement = 0x1,
  kImage = 0x2,
  kSetting = 0x3,
  kTitle = 0x4,
  kString = 0x5,
  kSecurity = 0x6
};

// Found by dumping the kSectionStringTable sections of various games:
enum class XdbfLocale : uint32_t {
  kUnknown = 0,
  kEnglish = 1,
  kJapanese = 2,
  kGerman = 3,
  kFrench = 4,
  kSpanish = 5,
  kItalian = 6,
  kKorean = 7,
  kChinese = 8,
};

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

#pragma pack(pop)

enum class XdbfAchievementType : uint32_t {
  kCompletion = 1,
  kLeveling = 2,
  kUnlock = 3,
  kEvent = 4,
  kTournament = 5,
  kCheckpoint = 6,
  kOther = 7,
};

enum class XdbfAchievementFlags : uint32_t {
  kTypeMask = 0x7,
  kShowUnachieved = 0x8,
  kAchievedOnline = 0x10000,
  kAchieved = 0x20000
};

struct XdbfAchievement {
  uint16_t id = 0;
  std::wstring label;
  std::wstring description;
  std::wstring unachieved_desc;
  uint32_t image_id = 0;
  uint32_t gamerscore = 0;
  uint32_t flags = 0;
  uint64_t unlock_time = 0;

  XdbfAchievementType GetType() {
    return static_cast<XdbfAchievementType>(
        flags & static_cast<uint32_t>(XdbfAchievementFlags::kTypeMask));
  }

  bool IsUnlocked() {
    return flags & static_cast<uint32_t>(XdbfAchievementFlags::kAchieved);
  }

  bool IsUnlockedOnline() {
    return flags & static_cast<uint32_t>(XdbfAchievementFlags::kAchievedOnline);
  }

  void Unlock(bool online = false) {
    flags |= static_cast<uint32_t>(XdbfAchievementFlags::kAchieved);
    if (online) {
      flags |= static_cast<uint32_t>(XdbfAchievementFlags::kAchievedOnline);
    }
    // TODO: set unlock time?
  }

  void Lock() {
    flags = flags & ~(static_cast<uint32_t>(XdbfAchievementFlags::kAchieved));
    flags =
        flags & ~(static_cast<uint32_t>(XdbfAchievementFlags::kAchievedOnline));
    unlock_time = 0;
  }
};

struct XdbfEntry {
  X_XDBF_ENTRY info;
  std::vector<uint8_t> data;
};

// Parses/creates an XDBF (XboxDataBaseFormat) file
// http://www.free60.org/wiki/XDBF
class XdbfFile {
 public:
  XdbfFile() {
    header.magic = 'XDBF';
    header.version = 1;
  }

  bool Read(const uint8_t* data, size_t data_size);
  bool Write(uint8_t* data, size_t* data_size);

  XdbfEntry* GetEntry(uint16_t section, uint64_t id) const;

  // Updates (or adds) an entry
  bool UpdateEntry(XdbfEntry entry);

 protected:
  X_XDBF_HEADER header;
  std::vector<XdbfEntry> entries;
  std::vector<X_XDBF_FILELOC> free_entries;
};

class SpaFile : public XdbfFile {
 public:
  std::string GetStringTableEntry(XdbfLocale locale, uint16_t string_id) const;

  uint32_t GetAchievements(XdbfLocale locale,
                           std::vector<XdbfAchievement>* achievements) const;

  XdbfEntry* GetIcon() const;
  XdbfLocale GetDefaultLocale() const;
  std::string GetTitle() const;
};

class GpdFile : public XdbfFile {
 public:
  bool GetAchievement(uint16_t id, XdbfAchievement* dest);
  uint32_t GetAchievements(std::vector<XdbfAchievement>* achievements) const;

  // Updates (or adds) an achievement
  bool UpdateAchievement(XdbfAchievement ach);
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_XDBF_UTILS_H_
