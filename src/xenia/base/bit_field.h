/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BIT_FIELD_H_
#define XENIA_BASE_BIT_FIELD_H_

#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace xe {

// Bitfield, where position starts at the LSB.
template <typename T, size_t position, size_t n_bits>
struct bf {
  bf() = default;
  inline operator T() const { return value(); }

  inline T value() const {
    return static_cast<T>((storage & mask()) >> position);
  }

  // For enum values, we strip them down to an underlying type.
  typedef
      typename std::conditional<std::is_enum<T>::value, std::underlying_type<T>,
                                std::remove_reference<T>>::type::type
          value_type;
  inline value_type mask() const {
    return (((value_type)~0) >> (8 * sizeof(value_type) - n_bits)) << position;
  }

  value_type storage;
};

}  // namespace xe

#endif  // XENIA_BASE_BIT_FIELD_H_
