/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/property.h"
#include "xenia/base/logging.h"

namespace xe {
namespace kernel {

Property::Property(uint32_t property_id, uint32_t value_size,
                   uint8_t* value_ptr) {
  property_id_.value = property_id;
  data_type_ = static_cast<X_USER_DATA_TYPE>(property_id_.type);

  value_size_ = value_size;
  value_.resize(value_size);

  if (value_ptr != 0) {
    memcpy(value_.data(), value_ptr, value_size);
  } else {
    XELOGW("{} Ctor: provided value_ptr is nullptr!", __func__);
  }
}

Property::Property(const uint8_t* serialized_data, size_t data_size) {
  if (data_size < 8) {
    XELOGW("Property::Property lacks information. Skipping!");
    return;
  }

  memcpy(&property_id_, serialized_data, sizeof(property_id_));
  data_type_ = static_cast<X_USER_DATA_TYPE>(property_id_.type);

  memcpy(&value_size_, serialized_data + 4, sizeof(value_size_));

  value_.resize(value_size_);
  memcpy(value_.data(), serialized_data + 8, value_size_);
}

Property::~Property() {};

std::vector<uint8_t> Property::Serialize() const {
  std::vector<uint8_t> serialized_property;
  serialized_property.resize(sizeof(property_id_) + sizeof(value_size_) +
                             value_.size());

  size_t offset = 0;
  memcpy(serialized_property.data(), &property_id_, sizeof(property_id_));
  offset += sizeof(property_id_);

  memcpy(serialized_property.data() + offset, &value_size_,
         sizeof(value_size_));

  offset += sizeof(value_size_);

  memcpy(serialized_property.data() + offset, value_.data(), value_.size());

  return serialized_property;
}

void Property::Write(Memory* memory, XUSER_PROPERTY* property) const {
  property->property_id = property_id_.value;
  property->data.type = data_type_;

  switch (data_type_) {
    case X_USER_DATA_TYPE::WSTRING:
      property->data.binary.size = value_size_;
      break;
    case X_USER_DATA_TYPE::CONTENT:
    case X_USER_DATA_TYPE::BINARY:
      property->data.binary.size = value_size_;
      // Property pointer must be valid at this point!
      memcpy(memory->TranslateVirtual(property->data.binary.ptr), value_.data(),
             value_size_);
      break;
    case X_USER_DATA_TYPE::INT32:
      memcpy(reinterpret_cast<uint8_t*>(&property->data.s32), value_.data(),
             value_size_);
      break;
    case X_USER_DATA_TYPE::INT64:
      memcpy(reinterpret_cast<uint8_t*>(&property->data.s64), value_.data(),
             value_size_);
      break;
    case X_USER_DATA_TYPE::DOUBLE:
      memcpy(reinterpret_cast<uint8_t*>(&property->data.f64), value_.data(),
             value_size_);
      break;
    case X_USER_DATA_TYPE::FLOAT:
      memcpy(reinterpret_cast<uint8_t*>(&property->data.f32), value_.data(),
             value_size_);
      break;
    case X_USER_DATA_TYPE::DATETIME:
      memcpy(reinterpret_cast<uint8_t*>(&property->data.filetime),
             value_.data(), value_size_);
      break;
    default:
      break;
  }
}

userDataVariant Property::GetValue() const {
  switch (data_type_) {
    case X_USER_DATA_TYPE::CONTENT:
    case X_USER_DATA_TYPE::BINARY:
      return value_;
    case X_USER_DATA_TYPE::INT32:
      return *reinterpret_cast<const uint32_t*>(value_.data());
    case X_USER_DATA_TYPE::INT64:
    case X_USER_DATA_TYPE::DATETIME:
      return *reinterpret_cast<const uint64_t*>(value_.data());
    case X_USER_DATA_TYPE::DOUBLE:
      return *reinterpret_cast<const double*>(value_.data());
    case X_USER_DATA_TYPE::WSTRING:
      return std::u16string(reinterpret_cast<const char16_t*>(value_.data()));
    case X_USER_DATA_TYPE::FLOAT:
      return *reinterpret_cast<const float*>(value_.data());
    default:
      break;
  }
  return value_;
}

}  // namespace kernel
}  // namespace xe