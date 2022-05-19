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
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

struct X_USER_PROFILE_SETTING_DATA {
  // UserProfile::Setting::Type. Appears to be 8-in-32 field, and the upper 24
  // are not always zeroed by the game.
  uint8_t type;
  uint8_t unk_1[3];
  xe::be<uint32_t> unk_4;
  // TODO(sabretooth): not sure if this is a union, but it seems likely.
  // Haven't run into cases other than "binary data" yet.
  union {
    xe::be<int32_t> s32;
    xe::be<int64_t> s64;
    xe::be<uint32_t> u32;
    xe::be<double> f64;
    struct {
      xe::be<uint32_t> size;
      xe::be<uint32_t> ptr;
    } unicode;
    xe::be<float> f32;
    struct {
      xe::be<uint32_t> size;
      xe::be<uint32_t> ptr;
    } binary;
    xe::be<uint64_t> filetime;
  };
};
static_assert_size(X_USER_PROFILE_SETTING_DATA, 16);

struct X_USER_PROFILE_SETTING {
  xe::be<uint32_t> from;
  xe::be<uint32_t> unk04;
  union {
    xe::be<uint32_t> user_index;
    xe::be<uint64_t> xuid;
  };
  xe::be<uint32_t> setting_id;
  xe::be<uint32_t> unk14;
  union {
    uint8_t data_bytes[sizeof(X_USER_PROFILE_SETTING_DATA)];
    X_USER_PROFILE_SETTING_DATA data;
  };
};
static_assert_size(X_USER_PROFILE_SETTING, 40);

class UserProfile {
 public:
  class SettingByteStream : public ByteStream {
   public:
    SettingByteStream(uint32_t ptr, uint8_t* data, size_t data_length,
                      size_t offset = 0)
        : ByteStream(data, data_length, offset), ptr_(ptr) {}

    uint32_t ptr() const { return static_cast<uint32_t>(ptr_ + offset()); }

   private:
    uint32_t ptr_;
  };
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
      UNSET = 0xFF,
    };
    union Key {
      uint32_t value;
      struct {
        uint32_t id : 14;
        uint32_t unk : 2;
        uint32_t size : 12;
        uint32_t type : 4;
      };
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
    virtual void Append(X_USER_PROFILE_SETTING_DATA* data,
                        SettingByteStream* stream) {
      data->type = static_cast<uint8_t>(type);
    }
    virtual std::vector<uint8_t> Serialize() const {
      return std::vector<uint8_t>();
    }
    virtual void Deserialize(std::vector<uint8_t>) {}
    bool is_title_specific() const { return (setting_id & 0x3F00) == 0x3F00; }
  };
  struct Int32Setting : public Setting {
    Int32Setting(uint32_t setting_id, int32_t value)
        : Setting(setting_id, Type::INT32, 4, true), value(value) {}
    int32_t value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->s32 = value;
    }
  };
  struct Int64Setting : public Setting {
    Int64Setting(uint32_t setting_id, int64_t value)
        : Setting(setting_id, Type::INT64, 8, true), value(value) {}
    int64_t value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->s64 = value;
    }
  };
  struct DoubleSetting : public Setting {
    DoubleSetting(uint32_t setting_id, double value)
        : Setting(setting_id, Type::DOUBLE, 8, true), value(value) {}
    double value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->f64 = value;
    }
  };
  struct UnicodeSetting : public Setting {
    UnicodeSetting(uint32_t setting_id, const std::u16string& value)
        : Setting(setting_id, Type::WSTRING, 8, true), value(value) {}
    std::u16string value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      if (value.empty()) {
        data->unicode.size = 0;
        data->unicode.ptr = 0;
      } else {
        size_t count = value.size() + 1;
        size_t size = 2 * count;
        assert_true(size <= std::numeric_limits<uint32_t>::max());
        data->unicode.size = static_cast<uint32_t>(size);
        data->unicode.ptr = stream->ptr();
        auto buffer =
            reinterpret_cast<uint16_t*>(&stream->data()[stream->offset()]);
        stream->Advance(size);
        xe::copy_and_swap(buffer, (uint16_t*)value.data(), count);
      }
    }
  };
  struct FloatSetting : public Setting {
    FloatSetting(uint32_t setting_id, float value)
        : Setting(setting_id, Type::FLOAT, 4, true), value(value) {}
    float value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->f32 = value;
    }
  };
  struct BinarySetting : public Setting {
    BinarySetting(uint32_t setting_id)
        : Setting(setting_id, Type::BINARY, 8, false), value() {}
    BinarySetting(uint32_t setting_id, const std::vector<uint8_t>& value)
        : Setting(setting_id, Type::BINARY, 8, true), value(value) {}
    std::vector<uint8_t> value;
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      if (value.empty()) {
        data->binary.size = 0;
        data->binary.ptr = 0;
      } else {
        size_t size = value.size();
        assert_true(size <= std::numeric_limits<uint32_t>::max());
        data->binary.size = static_cast<uint32_t>(size);
        data->binary.ptr = stream->ptr();
        stream->Write(value.data(), size);
      }
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
    void Append(X_USER_PROFILE_SETTING_DATA* data,
                SettingByteStream* stream) override {
      Setting::Append(data, stream);
      data->filetime = value;
    }
  };

  UserProfile(uint8_t index);

  uint64_t xuid() const { return xuid_; }
  std::string name() const { return name_; }
  uint32_t signin_state() const { return 1; }
  uint32_t type() const { return 1 | 2; /* local | online profile? */ }

  void AddSetting(std::unique_ptr<Setting> setting);
  Setting* GetSetting(uint32_t setting_id);

 private:
  uint64_t xuid_;
  std::string name_;
  std::vector<std::unique_ptr<Setting>> setting_list_;
  std::unordered_map<uint32_t, Setting*> settings_;

  void LoadSetting(UserProfile::Setting*);
  void SaveSetting(UserProfile::Setting*);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
