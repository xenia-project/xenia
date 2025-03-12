/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_data.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xam {

UserData::UserData() {}
UserData::~UserData() {}

UserData::UserData(const UserData& user_data) {
  data_ = user_data.data_;
  extended_data_ = user_data.extended_data_;
}

UserData::UserData(X_USER_DATA_TYPE data_type, UserDataTypes user_data) {
  data_.data = {};
  data_.type = data_type;

  switch (data_type) {
    case X_USER_DATA_TYPE::BINARY:
      extended_data_ = std::get<std::vector<uint8_t>>(user_data);
      data_.data.binary.size = static_cast<uint16_t>(extended_data_.size());
      break;
    case X_USER_DATA_TYPE::WSTRING: {
      std::u16string str = std::get<std::u16string>(user_data);
      data_.data.unicode.size =
          static_cast<uint16_t>(string_util::size_in_bytes(str));

      extended_data_.resize(data_.data.unicode.size);
      memcpy(extended_data_.data(), reinterpret_cast<uint8_t*>(str.data()),
             data_.data.unicode.size);
      break;
    }
    case X_USER_DATA_TYPE::INT32:
      data_.data.s32 = std::get<int32_t>(user_data);
      break;
    case X_USER_DATA_TYPE::FLOAT:
      data_.data.f32 = std::get<float>(user_data);
      break;
    case X_USER_DATA_TYPE::CONTEXT:
      data_.data.s32 = std::get<uint32_t>(user_data);
      break;
    case X_USER_DATA_TYPE::DOUBLE:
      data_.data.f64 = std::get<double>(user_data);
      break;
    case X_USER_DATA_TYPE::DATETIME:
    case X_USER_DATA_TYPE::INT64:
      data_.data.s64 = std::get<int64_t>(user_data);
      break;
    default:
      assert_always();
  }
}

UserData::UserData(const X_USER_DATA_TYPE data_type,
                   const uint32_t data_max_size, const X_USER_DATA* user_data) {
  memcpy(&data_, user_data, sizeof(X_USER_DATA));
  data_.type = data_type;

  if (requires_additional_data()) {
    data_.data.binary.size = std::min(
        static_cast<uint32_t>(data_.data.binary.size), kMaxUserDataSize);

    if (!data_.data.binary.size) {
      return;
    }

    extended_data_.resize(data_.data.binary.size);

    if (!user_data->data.binary.ptr) {
      return;
    }

    memcpy(
        extended_data_.data(),
        kernel_memory()->TranslateVirtual<uint8_t*>(user_data->data.binary.ptr),
        data_.data.binary.size);
  }
}

UserData::UserData(X_USER_DATA_TYPE data_type, std::span<const uint8_t> data) {
  data_.data = {};
  data_.type = data_type;

  if (!requires_additional_data()) {
    memcpy(&data_.data, data.data(),
           std::min(static_cast<size_t>(8), data.size()));
    return;
  }

  data_.data.binary.size = static_cast<uint32_t>(data.size());
  extended_data_.insert(extended_data_.begin(), data.begin(), data.end());
}

UserData::UserData(const X_USER_DATA_TYPE data_type,
                   const X_USER_DATA_UNION* user_data,
                   std::span<const uint8_t> extended_data) {
  data_.type = data_type;
  data_.data = *user_data;

  if (requires_additional_data()) {
    extended_data_.insert(extended_data_.begin(), extended_data.begin(),
                          extended_data.end());
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe