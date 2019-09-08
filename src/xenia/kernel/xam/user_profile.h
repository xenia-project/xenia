/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_PROFILE_H_
#define XENIA_KERNEL_XAM_USER_PROFILE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/kernel/xam/xdbf/xdbf.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// from https://github.com/xemio/testdev/blob/master/xkelib/xam/_xamext.h
#pragma pack(push, 4)
struct X_XAMACCOUNTINFO {
  enum AccountReservedFlags {
    kPasswordProtected = 0x10000000,
    kLiveEnabled = 0x20000000,
    kRecovering = 0x40000000,
    kVersionMask = 0x000000FF
  };

  enum AccountUserFlags {
    kPaymentInstrumentCreditCard = 1,

    kCountryMask = 0xFF00,
    kSubscriptionTierMask = 0xF00000,
    kLanguageMask = 0x3E000000,

    kParentalControlEnabled = 0x1000000,
  };

  enum AccountSubscriptionTier {
    kSubscriptionTierSilver = 3,
    kSubscriptionTierGold = 6,
    kSubscriptionTierFamilyGold = 9
  };

  // already exists inside xdbf.h??
  enum AccountLanguage {
    kNoLanguage,
    kEnglish,
    kJapanese,
    kGerman,
    kFrench,
    kSpanish,
    kItalian,
    kKorean,
    kTChinese,
    kPortuguese,
    kSChinese,
    kPolish,
    kRussian,
    kNorwegian = 15
  };

  enum AccountLiveFlags { kAcctRequiresManagement = 1 };

  xe::be<uint32_t> reserved_flags;
  xe::be<uint32_t> live_flags;
  wchar_t gamertag[0x10];
  xe::be<uint64_t> xuid_online;  // 09....
  xe::be<uint32_t> cached_user_flags;
  xe::be<uint32_t> network_id;
  char passcode[4];
  char online_domain[0x14];
  char online_kerberos_realm[0x18];
  char online_key[0x10];
  char passport_membername[0x72];
  char passport_password[0x20];
  char owner_passport_membername[0x72];

  bool IsPasscodeEnabled() {
    return (bool)(reserved_flags & AccountReservedFlags::kPasswordProtected);
  }

  bool IsLiveEnabled() {
    return (bool)(reserved_flags & AccountReservedFlags::kLiveEnabled);
  }

  bool IsRecovering() {
    return (bool)(reserved_flags & AccountReservedFlags::kRecovering);
  }

  bool IsPaymentInstrumentCreditCard() {
    return (bool)(cached_user_flags &
                  AccountUserFlags::kPaymentInstrumentCreditCard);
  }

  bool IsParentalControlled() {
    return (bool)(cached_user_flags &
                  AccountUserFlags::kParentalControlEnabled);
  }

  bool IsXUIDOffline() { return ((xuid_online >> 60) & 0xF) == 0xE; }
  bool IsXUIDOnline() { return ((xuid_online >> 48) & 0xFFFF) == 0x9; }
  bool IsXUIDValid() { return IsXUIDOffline() != IsXUIDOnline(); }
  bool IsTeamXUID() {
    return (xuid_online & 0xFF00000000000140) == 0xFE00000000000100;
  }

  uint32_t GetCountry() { return (cached_user_flags & kCountryMask) >> 8; }

  AccountSubscriptionTier GetSubscriptionTier() {
    return (AccountSubscriptionTier)(
        (cached_user_flags & kSubscriptionTierMask) >> 20);
  }

  AccountLanguage GetLanguage() {
    return (AccountLanguage)((cached_user_flags & kLanguageMask) >> 25);
  }

  std::string GetGamertagString() const;
};
// static_assert_size(X_XAMACCOUNTINFO, 0x17C);
#pragma pack(pop)

class UserProfile {
 public:
  struct Setting {
    enum class Type {
      CONTENT = 0,
      INT32 = 1,
      INT64 = 2,
      DOUBLE = 3,
      WSTRING = 4,
      FLOAT = 5,
      BINARY = 6,
      DATETIME = 7,
      INVALID = 0xFF,
    };
    union Key {
      struct {
        uint32_t id : 14;
        uint32_t unk : 2;
        uint32_t size : 12;
        uint32_t type : 4;
      };
      uint32_t value;
    };
    uint32_t setting_id;
    Type type;
    size_t size;
    bool is_set;
    uint32_t loaded_title_id;
    Setting(uint32_t setting_id, Type type, size_t size, bool is_set)
        : setting_id(setting_id),
          type(type),
          size(size),
          is_set(is_set),
          loaded_title_id(0) {}
    virtual size_t extra_size() const { return 0; }
    virtual size_t Append(uint8_t* user_data, uint8_t* buffer,
                          uint32_t buffer_ptr, size_t buffer_offset) {
      xe::store_and_swap<uint8_t>(user_data + kTypeOffset,
                                  static_cast<uint8_t>(type));
      return buffer_offset;
    }
    virtual std::vector<uint8_t> Serialize() const {
      return std::vector<uint8_t>();
    }
    virtual void Deserialize(std::vector<uint8_t>) {}
    bool is_title_specific() const { return (setting_id & 0x3F00) == 0x3F00; }

   protected:
    const size_t kTypeOffset = 0;
    const size_t kValueOffset = 8;
    const size_t kPointerOffset = 12;
  };
  struct Int32Setting : public Setting {
    Int32Setting(uint32_t setting_id, int32_t value)
        : Setting(setting_id, Type::INT32, 4, true), value(value) {}
    int32_t value;
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      xe::store_and_swap<int32_t>(user_data + kValueOffset, value);
      return buffer_offset;
    }
  };
  struct Int64Setting : public Setting {
    Int64Setting(uint32_t setting_id, int64_t value)
        : Setting(setting_id, Type::INT64, 8, true), value(value) {}
    int64_t value;
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      xe::store_and_swap<int64_t>(user_data + kValueOffset, value);
      return buffer_offset;
    }
  };
  struct DoubleSetting : public Setting {
    DoubleSetting(uint32_t setting_id, double value)
        : Setting(setting_id, Type::DOUBLE, 8, true), value(value) {}
    double value;
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      xe::store_and_swap<double>(user_data + kValueOffset, value);
      return buffer_offset;
    }
  };
  struct UnicodeSetting : public Setting {
    UnicodeSetting(uint32_t setting_id, const std::wstring& value)
        : Setting(setting_id, Type::WSTRING, 8, true), value(value) {}
    std::wstring value;
    size_t extra_size() const override {
      return value.empty() ? 0 : 2 * (static_cast<int32_t>(value.size()) + 1);
    }
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      int32_t length;
      if (value.empty()) {
        length = 0;
        xe::store_and_swap<int32_t>(user_data + kValueOffset, 0);
        xe::store_and_swap<uint32_t>(user_data + kPointerOffset, 0);
      } else {
        length = 2 * (static_cast<int32_t>(value.size()) + 1);
        xe::store_and_swap<int32_t>(user_data + kValueOffset, length);
        xe::store_and_swap<uint32_t>(
            user_data + kPointerOffset,
            buffer_ptr + static_cast<uint32_t>(buffer_offset));
        memcpy(buffer + buffer_offset, value.data(), length);
      }
      return buffer_offset + length;
    }
  };
  struct FloatSetting : public Setting {
    FloatSetting(uint32_t setting_id, float value)
        : Setting(setting_id, Type::FLOAT, 4, true), value(value) {}
    float value;
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      xe::store_and_swap<float>(user_data + kValueOffset, value);
      return buffer_offset;
    }
  };
  struct BinarySetting : public Setting {
    BinarySetting(uint32_t setting_id)
        : Setting(setting_id, Type::BINARY, 8, false), value() {}
    BinarySetting(uint32_t setting_id, const std::vector<uint8_t>& value)
        : Setting(setting_id, Type::BINARY, 8, true), value(value) {}
    std::vector<uint8_t> value;
    size_t extra_size() const override {
      return static_cast<int32_t>(value.size());
    }
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      int32_t length;
      if (value.empty()) {
        length = 0;
        xe::store_and_swap<int32_t>(user_data + kValueOffset, 0);
        xe::store_and_swap<int32_t>(user_data + kPointerOffset, 0);
      } else {
        length = static_cast<int32_t>(value.size());
        xe::store_and_swap<int32_t>(user_data + kValueOffset, length);
        xe::store_and_swap<uint32_t>(
            user_data + kPointerOffset,
            buffer_ptr + static_cast<uint32_t>(buffer_offset));
        memcpy(buffer + buffer_offset, value.data(), length);
      }
      return buffer_offset + length;
    }
    std::vector<uint8_t> Serialize() const override {
      return std::vector<uint8_t>(value.data(), value.data() + value.size());
    }
    void Deserialize(std::vector<uint8_t> data) override {
      value = data;
      is_set = true;
    }
  };
  struct DateTimeSetting : public Setting {
    DateTimeSetting(uint32_t setting_id, int64_t value)
        : Setting(setting_id, Type::DATETIME, 8, true), value(value) {}
    int64_t value;
    size_t Append(uint8_t* user_data, uint8_t* buffer, uint32_t buffer_ptr,
                  size_t buffer_offset) override {
      buffer_offset =
          Setting::Append(user_data, buffer, buffer_ptr, buffer_offset);
      xe::store_and_swap<int64_t>(user_data + kValueOffset, value);
      return buffer_offset;
    }
  };

  static bool DecryptAccountFile(const uint8_t* data, X_XAMACCOUNTINFO* output,
                                 bool devkit = false);

  static void EncryptAccountFile(const X_XAMACCOUNTINFO* input, uint8_t* output,
                                 bool devkit = false);

  UserProfile();

  uint64_t xuid() const { return account_.xuid_online; }
  std::string name() const { return account_.GetGamertagString(); }
  // uint32_t signin_state() const { return 1; }

  void AddSetting(std::unique_ptr<Setting> setting);
  Setting* GetSetting(uint32_t setting_id);

  xdbf::GpdFile* SetTitleSpaData(const xdbf::SpaFile& spa_data);
  xdbf::GpdFile* GetTitleGpd(uint32_t title_id = -1);

  void GetTitles(std::vector<xdbf::GpdFile*>& titles);

  bool UpdateTitleGpd(uint32_t title_id = -1);
  bool UpdateAllGpds();

 private:
  void LoadProfile();
  bool UpdateGpd(uint32_t title_id, xdbf::GpdFile& gpd_data);

  X_XAMACCOUNTINFO account_;
  std::vector<std::unique_ptr<Setting>> setting_list_;
  std::unordered_map<uint32_t, Setting*> settings_;

  void LoadSetting(UserProfile::Setting*);
  void SaveSetting(UserProfile::Setting*);

  std::unordered_map<uint32_t, xdbf::GpdFile> title_gpds_;
  xdbf::GpdFile dash_gpd_;
  xdbf::GpdFile* curr_gpd_ = nullptr;
  uint32_t curr_title_id_ = -1;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
