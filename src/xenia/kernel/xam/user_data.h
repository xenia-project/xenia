/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_DATA_H_
#define XENIA_KERNEL_XAM_USER_DATA_H_

#include <span>
#include <variant>
#include <vector>

#include "xenia/base/string_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

union AttributeKey {
  uint32_t value;
  struct {
    uint32_t id : 14;
    uint32_t unk : 2;
    uint32_t size : 12;
    uint32_t type : 4;
  };
};

// ToDo: Check if setting CAN be set to unset. Property can and it will be
// interpreted as a basic type property.
enum class X_USER_DATA_TYPE : uint8_t {
  CONTEXT = 0,
  INT32 = 1,
  INT64 = 2,
  DOUBLE = 3,
  WSTRING = 4,
  FLOAT = 5,
  BINARY = 6,
  DATETIME = 7,
  UNSET = 0xFF,
};

struct X_USER_DATA_UNION {
  union {
    be<int32_t> s32;
    be<int64_t> s64;
    be<uint32_t> u32;
    be<double> f64;
    struct {
      be<uint32_t> size;
      be<uint32_t> ptr;
    } unicode;
    be<float> f32;
    struct {
      be<uint32_t> size;
      be<uint32_t> ptr;
    } binary;
    be<uint64_t> filetime;
  };
};
static_assert_size(X_USER_DATA_UNION, 8);

struct alignas(8) X_USER_DATA {
  X_USER_DATA_TYPE type;
  X_USER_DATA_UNION data;
};
static_assert_size(X_USER_DATA, 16);

using UserDataTypes = std::variant<uint32_t, int32_t, float, int64_t, double,
                                   std::u16string, std::vector<uint8_t>>;

constexpr uint32_t kMaxUserDataSize = 0x03E8;

class UserData {
 public:
  X_USER_DATA_TYPE get_type() const { return data_.type; }

  const X_USER_DATA* get_data() const { return &data_; }
  std::span<const uint8_t> get_extended_data() const {
    return {extended_data_.data(), extended_data_.size()};
  }

  UserDataTypes get_host_data() const {
    if (data_.type == X_USER_DATA_TYPE::INT32) {
      return data_.data.s32;
    }

    if (data_.type == X_USER_DATA_TYPE::DATETIME) {
      return data_.data.s64;
    }

    if (data_.type == X_USER_DATA_TYPE::WSTRING) {
      if (get_extended_data().empty()) {
        return std::u16string();
      }

      const char16_t* str_begin =
          reinterpret_cast<const char16_t*>(get_extended_data().data());

      return string_util::read_u16string_and_swap(str_begin);
    }

    return 0;
  }

  bool is_valid_type() const {
    return data_.type >= X_USER_DATA_TYPE::CONTEXT &&
           data_.type <= X_USER_DATA_TYPE::DATETIME;
  }

  bool requires_additional_data() const {
    return data_.type == X_USER_DATA_TYPE::BINARY ||
           data_.type == X_USER_DATA_TYPE::WSTRING;
  }

  static X_USER_DATA_TYPE get_type(uint32_t id) {
    return static_cast<X_USER_DATA_TYPE>(id >> 28);
  }
  static uint16_t get_max_size(uint32_t id) {
    return static_cast<uint16_t>(id >> 16) & kMaxUserDataSize;
  }

  static bool requires_additional_data(uint32_t id) {
    const auto type = get_type(id);

    return type == X_USER_DATA_TYPE::BINARY ||
           type == X_USER_DATA_TYPE::WSTRING;
  }

  static uint32_t get_valid_data_size(uint32_t id, uint32_t provided_size) {
    if (!requires_additional_data(id)) {
      if (provided_size == sizeof(uint32_t) ||
          provided_size == sizeof(uint64_t)) {
        return provided_size;
      }

      switch (get_type(id)) {
        case X_USER_DATA_TYPE::CONTEXT:
        case X_USER_DATA_TYPE::FLOAT:
        case X_USER_DATA_TYPE::INT32:
          return sizeof(uint32_t);

        case X_USER_DATA_TYPE::DATETIME:
        case X_USER_DATA_TYPE::DOUBLE:
        case X_USER_DATA_TYPE::INT64:
          return sizeof(uint64_t);
      }

      return sizeof(uint64_t);
    }

    if (!provided_size) {
      return kMaxUserDataSize;
    }

    return std::min(static_cast<uint32_t>(get_max_size(id)), provided_size);
  }

  size_t get_data_size() const {
    return sizeof(X_USER_DATA) + extended_data_.size();
  }

  // Settings specific
  static size_t get_data_size(uint32_t id, const X_USER_DATA* user_data) {
    if (requires_additional_data(id)) {
      return std::min(get_max_size(id),
                      static_cast<uint16_t>(user_data->data.binary.size));
    }

    return get_max_size(id);
  }

 protected:
  UserData() = default;
  UserData(const UserData& user_data);

  // From host
  UserData(X_USER_DATA_TYPE data_type, UserDataTypes user_data);
  // From guest
  UserData(const X_USER_DATA_TYPE data_type, const X_USER_DATA* user_data);

  // From guest - Property specific ctor. Property transfer raw data directly.
  UserData(X_USER_DATA_TYPE data_type, std::span<const uint8_t> data);
  UserData(std::span<const uint8_t> data);

  // For data from GPD
  UserData(const X_USER_DATA_TYPE data_type, const X_USER_DATA_UNION* user_data,
           std::span<const uint8_t> extended_data);

  ~UserData() = default;

  X_USER_DATA data_ = {};
  std::vector<uint8_t> extended_data_ = {};
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif