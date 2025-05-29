/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_PROPERTY_H_
#define XENIA_KERNEL_XAM_USER_PROPERTY_H_

#include <variant>

#include "xenia/kernel/xam/user_data.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// This structure is here because context is a one type of property.
struct XUSER_CONTEXT {
  xe::be<uint32_t> context_id;
  xe::be<uint32_t> value;
};
static_assert_size(XUSER_CONTEXT, 0x8);

struct XUSER_PROPERTY {
  xe::be<uint32_t> property_id;
  X_USER_DATA data;
};
static_assert_size(XUSER_PROPERTY, 0x18);

class Property : public UserData {
 public:
  Property() = default;
  Property(const Property& property);

  Property(uint32_t property_id, UserDataTypes property_data);
  // Ctor used while guest is creating property.
  Property(uint32_t property_id, uint32_t value_size, uint8_t* value_ptr);
  // Ctor used for deserialization
  Property(const uint8_t* serialized_data, size_t data_size);
  Property(std::span<const uint8_t> serialized_data);

  ~Property() = default;

  const AttributeKey GetPropertyId() const { return property_id_; }

  bool IsValid() const { return property_id_.value != 0; }
  bool IsContext() const { return data_.type == X_USER_DATA_TYPE::CONTEXT; }
  void WriteToGuest(XUSER_PROPERTY* property) const;
  std::vector<uint8_t> Serialize() const;

 private:
  AttributeKey property_id_ = {};
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif