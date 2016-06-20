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

#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

class UserProfile {
 public:
  struct Setting {
    enum class Type {
      UNKNOWN = 0,
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
    Setting(uint32_t setting_id, Type type, size_t size, bool is_set)
        : setting_id(setting_id), type(type), size(size), is_set(is_set) {}
    virtual size_t extra_size() const { return 0; }
    virtual size_t Append(uint8_t* user_data, uint8_t* buffer,
                          uint32_t buffer_ptr, size_t buffer_offset) {
      xe::store_and_swap<uint8_t>(user_data + kTypeOffset,
                                  static_cast<uint8_t>(type));
      return buffer_offset;
    }
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

  UserProfile();

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return name_; }
  uint32_t signin_state() const { return 1; }

  void AddSetting(std::unique_ptr<Setting> setting);
  Setting* GetSetting(uint32_t setting_id);

 private:
  uint64_t xuid_;
  std::string name_;
  std::vector<std::unique_ptr<Setting>> setting_list_;
  std::unordered_map<uint32_t, Setting*> settings_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
