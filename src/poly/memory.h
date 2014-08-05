/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_MEMORY_H_
#define POLY_MEMORY_H_

#include <string>

#include <poly/byte_order.h>

namespace poly {

size_t page_size();

template <typename T>
T load(const void* mem);
template <>
inline int8_t load<int8_t>(const void* mem) {
  return *reinterpret_cast<const int8_t*>(mem);
}
template <>
inline uint8_t load<uint8_t>(const void* mem) {
  return *reinterpret_cast<const uint8_t*>(mem);
}
template <>
inline int16_t load<int16_t>(const void* mem) {
  return *reinterpret_cast<const int16_t*>(mem);
}
template <>
inline uint16_t load<uint16_t>(const void* mem) {
  return *reinterpret_cast<const uint16_t*>(mem);
}
template <>
inline int32_t load<int32_t>(const void* mem) {
  return *reinterpret_cast<const int32_t*>(mem);
}
template <>
inline uint32_t load<uint32_t>(const void* mem) {
  return *reinterpret_cast<const uint32_t*>(mem);
}
template <>
inline int64_t load<int64_t>(const void* mem) {
  return *reinterpret_cast<const int64_t*>(mem);
}
template <>
inline uint64_t load<uint64_t>(const void* mem) {
  return *reinterpret_cast<const uint64_t*>(mem);
}
template <>
inline float load<float>(const void* mem) {
  return *reinterpret_cast<const float*>(mem);
}
template <>
inline double load<double>(const void* mem) {
  return *reinterpret_cast<const double*>(mem);
}

template <typename T>
T load_and_swap(const void* mem);
template <>
inline int8_t load_and_swap<int8_t>(const void* mem) {
  return *reinterpret_cast<const int8_t*>(mem);
}
template <>
inline uint8_t load_and_swap<uint8_t>(const void* mem) {
  return *reinterpret_cast<const uint8_t*>(mem);
}
template <>
inline int16_t load_and_swap<int16_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const int16_t*>(mem));
}
template <>
inline uint16_t load_and_swap<uint16_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const uint16_t*>(mem));
}
template <>
inline int32_t load_and_swap<int32_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const int32_t*>(mem));
}
template <>
inline uint32_t load_and_swap<uint32_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const uint32_t*>(mem));
}
template <>
inline int64_t load_and_swap<int64_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const int64_t*>(mem));
}
template <>
inline uint64_t load_and_swap<uint64_t>(const void* mem) {
  return byte_swap(*reinterpret_cast<const uint64_t*>(mem));
}
template <>
inline float load_and_swap<float>(const void* mem) {
  return byte_swap(*reinterpret_cast<const float*>(mem));
}
template <>
inline double load_and_swap<double>(const void* mem) {
  return byte_swap(*reinterpret_cast<const double*>(mem));
}
template <>
inline std::string load_and_swap<std::string>(const void* mem) {
  std::string value;
  for (int i = 0;; ++i) {
    auto c =
        poly::load_and_swap<uint8_t>(reinterpret_cast<const uint8_t*>(mem) + i);
    if (!c) {
      break;
    }
    value.push_back(static_cast<char>(c));
  }
  return value;
}
template <>
inline std::wstring load_and_swap<std::wstring>(const void* mem) {
  std::wstring value;
  for (int i = 0;; ++i) {
    auto c = poly::load_and_swap<uint16_t>(
        reinterpret_cast<const uint16_t*>(mem) + i);
    if (!c) {
      break;
    }
    value.push_back(static_cast<wchar_t>(c));
  }
  return value;
}

template <typename T>
void store(void* mem, T value);
template <>
inline void store<int8_t>(void* mem, int8_t value) {
  *reinterpret_cast<int8_t*>(mem) = value;
}
template <>
inline void store<uint8_t>(void* mem, uint8_t value) {
  *reinterpret_cast<uint8_t*>(mem) = value;
}
template <>
inline void store<int16_t>(void* mem, int16_t value) {
  *reinterpret_cast<int16_t*>(mem) = value;
}
template <>
inline void store<uint16_t>(void* mem, uint16_t value) {
  *reinterpret_cast<uint16_t*>(mem) = value;
}
template <>
inline void store<int32_t>(void* mem, int32_t value) {
  *reinterpret_cast<int32_t*>(mem) = value;
}
template <>
inline void store<uint32_t>(void* mem, uint32_t value) {
  *reinterpret_cast<uint32_t*>(mem) = value;
}
template <>
inline void store<int64_t>(void* mem, int64_t value) {
  *reinterpret_cast<int64_t*>(mem) = value;
}
template <>
inline void store<uint64_t>(void* mem, uint64_t value) {
  *reinterpret_cast<uint64_t*>(mem) = value;
}
template <>
inline void store<float>(void* mem, float value) {
  *reinterpret_cast<float*>(mem) = value;
}
template <>
inline void store<double>(void* mem, double value) {
  *reinterpret_cast<double*>(mem) = value;
}

template <typename T>
void store_and_swap(void* mem, T value);
template <>
inline void store_and_swap<int8_t>(void* mem, int8_t value) {
  *reinterpret_cast<int8_t*>(mem) = value;
}
template <>
inline void store_and_swap<uint8_t>(void* mem, uint8_t value) {
  *reinterpret_cast<uint8_t*>(mem) = value;
}
template <>
inline void store_and_swap<int16_t>(void* mem, int16_t value) {
  *reinterpret_cast<int16_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint16_t>(void* mem, uint16_t value) {
  *reinterpret_cast<uint16_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<int32_t>(void* mem, int32_t value) {
  *reinterpret_cast<int32_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint32_t>(void* mem, uint32_t value) {
  *reinterpret_cast<uint32_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<int64_t>(void* mem, int64_t value) {
  *reinterpret_cast<int64_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint64_t>(void* mem, uint64_t value) {
  *reinterpret_cast<uint64_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<float>(void* mem, float value) {
  *reinterpret_cast<float*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<double>(void* mem, double value) {
  *reinterpret_cast<double*>(mem) = byte_swap(value);
}

}  // namespace poly

#endif  // POLY_MEMORY_H_
