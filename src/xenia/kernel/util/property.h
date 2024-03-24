/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_PROPERTY_H_
#define XENIA_KERNEL_UTIL_PROPERTY_H_

#include <variant>

#include "xenia/base/byte_stream.h"
#include "xenia/kernel/util/xuserdata.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

struct XUSER_PROPERTY {
  xe::be<uint32_t> property_id;
  X_USER_DATA data;
};

using userDataVariant = std::variant<uint32_t, uint64_t, float, double,
                                     std::u16string, std::vector<uint8_t> >;

class Property {
 public:
  // Ctor used while guest is creating property.
  Property(uint32_t property_id, uint32_t value_size, uint8_t* value_ptr);
  // Ctor used for deserialization
  Property(const uint8_t* serialized_data, size_t data_size);
  ~Property();

  const AttributeKey GetPropertyId() const { return property_id_; }

  bool IsValid() const { return property_id_.value != 0; }
  std::vector<uint8_t> Serialize() const;

  // Writer back to guest structure
  void Write(Memory* memory, XUSER_PROPERTY* property) const;
  uint32_t GetSize() const { return value_size_; }

  bool RequiresPointer() const {
    return static_cast<X_USER_DATA_TYPE>(property_id_.type) ==
               X_USER_DATA_TYPE::CONTENT ||
           static_cast<X_USER_DATA_TYPE>(property_id_.type) ==
               X_USER_DATA_TYPE::WSTRING ||
           static_cast<X_USER_DATA_TYPE>(property_id_.type) ==
               X_USER_DATA_TYPE::BINARY;
  }

  // Returns variant for specific value in LE (host) notation.
  userDataVariant GetValue() const;

 private:
  AttributeKey property_id_ = {};
  X_USER_DATA_TYPE data_type_ = X_USER_DATA_TYPE::UNSET;

  uint32_t value_size_ = 0;
  std::vector<uint8_t> value_;
};

}  // namespace kernel
}  // namespace xe

#endif