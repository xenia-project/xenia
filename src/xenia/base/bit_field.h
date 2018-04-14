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
  // For enum values, we strip them down to an underlying type.
  typedef
      typename std::conditional<std::is_enum<T>::value, std::underlying_type<T>,
                                std::remove_reference<T>>::type::type
          value_type;

  bf() = default;
  inline operator T() const { return value(); }

  inline T value() const {
    auto value = (storage & mask()) >> position;
    if (std::is_signed<value_type>::value) {
      // If the value is signed, sign-extend it.
      value_type sign_mask = value_type(1) << (n_bits - 1);
      value = (sign_mask ^ value) - sign_mask;
    }

    return static_cast<T>(value);
  }

  inline value_type mask() const {
    return ((value_type(1) << n_bits) - 1) << position;
  }

  value_type storage;
};

}  // namespace xe

#endif  // XENIA_BASE_BIT_FIELD_H_
