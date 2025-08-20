/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_PROFILE_H_
#define XENIA_KERNEL_XAM_USER_PROFILE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/byte_stream.h"
#include "xenia/kernel/util/xuserdata.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

constexpr uint32_t kMaxSettingSize = 0x03E8;

enum class X_USER_PROFILE_SETTING_SOURCE : uint32_t {
  NOT_SET = 0,
  DEFAULT = 1,
  TITLE = 2,
  UNKNOWN = 3,
};

// Each setting contains 0x18 bytes long header
struct X_USER_PROFILE_SETTING_HEADER {
  xe::be<uint32_t> setting_id;
  xe::be<uint32_t> unknown_1;
  xe::be<uint8_t> setting_type;
  char unknown_2[3];
  xe::be<uint32_t> unknown_3;

  union {
    // Size is used only for types: CONTENT, WSTRING, BINARY
    be<uint32_t> size;
    // Raw values that can be written. They do not need to be serialized.
    be<int32_t> s32;
    be<int64_t> s64;
    be<uint32_t> u32;
    be<double> f64;
    be<float> f32;
  };
};
static_assert_size(X_USER_PROFILE_SETTING_HEADER, 0x18);

struct X_USER_PROFILE_SETTING {
  xe::be<uint32_t> from;
  union {
    xe::be<uint32_t> user_index;
    xe::be<uint64_t> xuid;
  };
  xe::be<uint32_t> setting_id;
  union {
    uint8_t data_bytes[sizeof(X_USER_DATA)];
    X_USER_DATA data;
  };
};
static_assert_size(X_USER_PROFILE_SETTING, 40);

class UserSetting {
 public:
  template <typename T>
  UserSetting(uint32_t setting_id, T data) {
    header_.setting_id = setting_id;

    setting_id_.value = setting_id;
    CreateUserData(setting_id, data);
  }

  static bool is_title_specific(uint32_t setting_id) {
    return (setting_id & 0x3F00) == 0x3F00;
  }

  bool is_title_specific() const {
    return is_title_specific(setting_id_.value);
  }

  const uint32_t GetSettingId() const { return setting_id_.value; }
  const X_USER_PROFILE_SETTING_SOURCE GetSettingSource() const {
    return created_by_;
  }
  const X_USER_PROFILE_SETTING_HEADER* GetSettingHeader() const {
    return &header_;
  }
  UserData* GetSettingData() { return user_data_.get(); }

  void SetNewSettingSource(X_USER_PROFILE_SETTING_SOURCE new_source) {
    created_by_ = new_source;
  }

  void SetNewSettingHeader(X_USER_PROFILE_SETTING_HEADER* header) {
    header_ = *header;
  }

 private:
  void CreateUserData(uint32_t setting_id, uint32_t data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::INT32);
    header_.s64 = data;
    user_data_ = std::make_unique<Uint32UserData>(data);
  }
  void CreateUserData(uint32_t setting_id, int32_t data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::INT32);
    header_.s32 = data;
    user_data_ = std::make_unique<Int32UserData>(data);
  }
  void CreateUserData(uint32_t setting_id, float data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::FLOAT);
    header_.f32 = data;
    user_data_ = std::make_unique<FloatUserData>(data);
  }
  void CreateUserData(uint32_t setting_id, double data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::DOUBLE);
    header_.f64 = data;
    user_data_ = std::make_unique<DoubleUserData>(data);
  }
  void CreateUserData(uint32_t setting_id, int64_t data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::INT64);
    header_.s64 = data;
    user_data_ = std::make_unique<Int64UserData>(data);
  }
  void CreateUserData(uint32_t setting_id, const std::u16string& data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::WSTRING);
    header_.size =
        std::min(kMaxSettingSize, static_cast<uint32_t>((data.size() + 1) * 2));
    user_data_ = std::make_unique<UnicodeUserData>(data);
  }
  void CreateUserData(uint32_t setting_id, const std::vector<uint8_t>& data) {
    header_.setting_type = static_cast<uint8_t>(X_USER_DATA_TYPE::BINARY);
    header_.size =
        std::min(kMaxSettingSize, static_cast<uint32_t>(data.size()));
    user_data_ = std::make_unique<BinaryUserData>(data);
  }

  X_USER_PROFILE_SETTING_SOURCE created_by_ =
      X_USER_PROFILE_SETTING_SOURCE::DEFAULT;

  X_USER_PROFILE_SETTING_HEADER header_ = {};
  UserData::Key setting_id_ = {};
  std::unique_ptr<UserData> user_data_ = nullptr;
};

class UserProfile {
 public:
  UserProfile();

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return name_; }
  uint32_t signin_state() const { return 1; }
  uint32_t type() const { return 1 | 2; /* local | online profile? */ }

  void AddSetting(std::unique_ptr<UserSetting> setting);
  UserSetting* GetSetting(uint32_t setting_id);

 private:
  uint64_t xuid_;
  std::string name_;
  std::vector<std::unique_ptr<UserSetting>> setting_list_;
  std::unordered_map<uint32_t, UserSetting*> settings_;

  void LoadSetting(UserSetting*);
  void SaveSetting(UserSetting*);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
