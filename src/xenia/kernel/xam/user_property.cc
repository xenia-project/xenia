/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_property.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xam {

Property::Property(const Property& property)
    : UserData(property), property_id_(property.property_id_) {}

Property::Property(uint32_t property_id, UserDataTypes property_data)
    : UserData(get_type(property_id), property_data) {
  property_id_ = static_cast<AttributeKey>(property_id);
}

Property::Property(uint32_t property_id, uint32_t value_size,
                   uint8_t* value_ptr)
    : UserData(get_type(property_id),
               std::span<const uint8_t>(value_ptr, value_size)) {
  property_id_.value = property_id;
}

Property::Property(const uint8_t* serialized_data, size_t data_size)
    : UserData(std::span<const uint8_t>(serialized_data, data_size)) {
  property_id_.value = *reinterpret_cast<const uint32_t*>(serialized_data);
}

Property::Property(std::span<const uint8_t> serialized_data)
    : UserData(serialized_data) {
  property_id_.value =
      *reinterpret_cast<const uint32_t*>(serialized_data.data());
}

std::vector<uint8_t> Property::Serialize() const {
  std::vector<uint8_t> serialized_property(
      sizeof(AttributeKey) + sizeof(X_USER_DATA) + extended_data_.size());

  memcpy(serialized_property.data(), &property_id_, sizeof(AttributeKey));
  memcpy(serialized_property.data() + sizeof(AttributeKey), &data_,
         sizeof(X_USER_DATA));

  if (requires_additional_data()) {
    memcpy(
        serialized_property.data() + sizeof(AttributeKey) + sizeof(X_USER_DATA),
        extended_data_.data(), extended_data_.size());
  }
  return serialized_property;
}

void Property::WriteToGuest(XUSER_PROPERTY* property) const {
  if (!property) {
    return;
  }

  property->property_id = property_id_.value;

  if (requires_additional_data()) {
    property->data.type = data_.type;
    property->data.data.binary.size =
        static_cast<uint32_t>(extended_data_.size());

    memcpy(kernel_memory()->TranslateVirtual(property->data.data.binary.ptr),
           extended_data_.data(), extended_data_.size());
  } else {
    memcpy(&property->data, &data_, sizeof(X_USER_DATA));
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe