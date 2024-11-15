/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MEMORY_H_
#define XENIA_BASE_MEMORY_H_

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/platform.h"

namespace xe {
namespace memory {

// For variable declarations (not return values or `this` pointer).
// Not propagated.
#define XE_RESTRICT_VAR __restrict

// Aliasing-safe bit reinterpretation.
// For more complex cases such as non-trivially-copyable types, write copying
// code respecting the requirements for them externally instead of using these
// functions.

template <typename Dst, typename Src>
void Reinterpret(Dst& XE_RESTRICT_VAR dst, const Src& XE_RESTRICT_VAR src) {
  static_assert(sizeof(Dst) == sizeof(Src));
  static_assert(std::is_trivially_copyable_v<Dst>);
  static_assert(std::is_trivially_copyable_v<Src>);
  std::memcpy(&dst, &src, sizeof(Dst));
}

template <typename Dst, typename Src>
Dst Reinterpret(const Src& XE_RESTRICT_VAR src) {
  Dst dst;
  Reinterpret(dst, src);
  return dst;
}

#if XE_PLATFORM_ANDROID
void AndroidInitialize();
void AndroidShutdown();
#endif

// Returns the native page size of the system, in bytes.
// This should be ~4KiB.
size_t page_size();

// Returns the allocation granularity of the system, in bytes.
// This is likely 64KiB.
size_t allocation_granularity();

enum class PageAccess {
  kNoAccess = 0,
  kReadOnly = 1 << 0,
  kReadWrite = kReadOnly | 1 << 1,
  kExecuteReadOnly = kReadOnly | 1 << 2,
  kExecuteReadWrite = kReadWrite | 1 << 2,
};

enum class AllocationType {
  kReserve = 1 << 0,
  kCommit = 1 << 1,
  kReserveCommit = kReserve | kCommit,
};

enum class DeallocationType {
  kRelease = 1 << 0,
  kDecommit = 1 << 1,
};

// Whether the host allows the pages to be allocated or mapped with
// PageAccess::kExecuteReadWrite - if not, separate mappings backed by the same
// memory-mapped file must be used to write to executable pages.
bool IsWritableExecutableMemorySupported();

// Whether PageAccess::kExecuteReadWrite is a supported and preferred way of
// writing executable memory, useful for simulating how Xenia would work without
// writable executable memory on a system with it.
bool IsWritableExecutableMemoryPreferred();

// Allocates a block of memory at the given page-aligned base address.
// Fails if the memory is not available.
// Specify nullptr for base_address to leave it up to the system.
void* AllocFixed(void* base_address, size_t length,
                 AllocationType allocation_type, PageAccess access);

// Deallocates and/or releases the given block of memory.
// When releasing memory length must be zero, as all pages in the region are
// released.
bool DeallocFixed(void* base_address, size_t length,
                  DeallocationType deallocation_type);

// Sets the access rights for the given block of memory and returns the previous
// access rights. Both base_address and length will be adjusted to page_size().
bool Protect(void* base_address, size_t length, PageAccess access,
             PageAccess* out_old_access = nullptr);

// Queries a region of pages to get the access rights. This will modify the
// length parameter to the length of pages with the same consecutive access
// rights. The length will start from the first byte of the first page of
// the region.
bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out);

// Allocates a block of memory for a type with the given alignment.
// The memory must be freed with AlignedFree.
template <typename T>
inline T* AlignedAlloc(size_t alignment) {
#if XE_COMPILER_MSVC
  return reinterpret_cast<T*>(_aligned_malloc(sizeof(T), alignment));
#else
  void* ptr = nullptr;
  if (posix_memalign(&ptr, alignment, sizeof(T))) {
    return nullptr;
  }
  return reinterpret_cast<T*>(ptr);
#endif  // XE_COMPILER_MSVC
}

// Frees memory previously allocated with AlignedAlloc.
template <typename T>
void AlignedFree(T* ptr) {
#if XE_COMPILER_MSVC
  _aligned_free(ptr);
#else
  free(ptr);
#endif  // XE_COMPILER_MSVC
}

#if XE_PLATFORM_WIN32
// HANDLE.
typedef void* FileMappingHandle;
constexpr FileMappingHandle kFileMappingHandleInvalid = nullptr;
#else
// File descriptor.
typedef int FileMappingHandle;
constexpr FileMappingHandle kFileMappingHandleInvalid = -1;
#endif

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path,
                                          size_t length, PageAccess access,
                                          bool commit);
void CloseFileMappingHandle(FileMappingHandle handle,
                            const std::filesystem::path& path);
void* MapFileView(FileMappingHandle handle, void* base_address, size_t length,
                  PageAccess access, size_t file_offset);
bool UnmapFileView(FileMappingHandle handle, void* base_address, size_t length);

inline size_t hash_combine(size_t seed) { return seed; }

template <typename T, typename... Ts>
size_t hash_combine(size_t seed, const T& v, const Ts&... vs) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
  return hash_combine(seed, vs...);
}

}  // namespace memory

// TODO(benvanik): move into xe::memory::

inline void* low_address(void* address) {
  return reinterpret_cast<void*>(uint64_t(address) & 0xFFFFFFFF);
}

void copy_128_aligned(void* dest, const void* src, size_t count);

void copy_and_swap_16_aligned(void* dest, const void* src, size_t count);
void copy_and_swap_16_unaligned(void* dest, const void* src, size_t count);
void copy_and_swap_32_aligned(void* dest, const void* src, size_t count);
void copy_and_swap_32_unaligned(void* dest, const void* src, size_t count);
void copy_and_swap_64_aligned(void* dest, const void* src, size_t count);
void copy_and_swap_64_unaligned(void* dest, const void* src, size_t count);
void copy_and_swap_16_in_32_aligned(void* dest, const void* src, size_t count);
void copy_and_swap_16_in_32_unaligned(void* dest, const void* src,
                                      size_t count);

template <typename T>
void copy_and_swap(T* dest, const T* src, size_t count) {
  bool is_aligned = reinterpret_cast<uintptr_t>(dest) % 32 == 0 &&
                    reinterpret_cast<uintptr_t>(src) % 32 == 0;
  if (sizeof(T) == 1) {
    std::memcpy(dest, src, count);
  } else if (sizeof(T) == 2) {
    auto ps = reinterpret_cast<const uint16_t*>(src);
    auto pd = reinterpret_cast<uint16_t*>(dest);
    if (is_aligned) {
      copy_and_swap_16_aligned(pd, ps, count);
    } else {
      copy_and_swap_16_unaligned(pd, ps, count);
    }
  } else if (sizeof(T) == 4) {
    auto ps = reinterpret_cast<const uint32_t*>(src);
    auto pd = reinterpret_cast<uint32_t*>(dest);
    if (is_aligned) {
      copy_and_swap_32_aligned(pd, ps, count);
    } else {
      copy_and_swap_32_unaligned(pd, ps, count);
    }
  } else if (sizeof(T) == 8) {
    auto ps = reinterpret_cast<const uint64_t*>(src);
    auto pd = reinterpret_cast<uint64_t*>(dest);
    if (is_aligned) {
      copy_and_swap_64_aligned(pd, ps, count);
    } else {
      copy_and_swap_64_unaligned(pd, ps, count);
    }
  } else {
    assert_always("Invalid xe::copy_and_swap size");
  }
}

template <typename T>
T load(const void* mem);

// Specializations using memcpy to avoid alignment issues
template <>
inline int8_t load<int8_t>(const void* mem) {
  int8_t value;
  std::memcpy(&value, mem, sizeof(int8_t));
  return value;
}

template <>
inline uint8_t load<uint8_t>(const void* mem) {
  uint8_t value;
  std::memcpy(&value, mem, sizeof(uint8_t));
  return value;
}

template <>
inline int16_t load<int16_t>(const void* mem) {
  int16_t value;
  std::memcpy(&value, mem, sizeof(int16_t));
  return value;
}

template <>
inline uint16_t load<uint16_t>(const void* mem) {
  uint16_t value;
  std::memcpy(&value, mem, sizeof(uint16_t));
  return value;
}

template <>
inline int32_t load<int32_t>(const void* mem) {
  int32_t value;
  std::memcpy(&value, mem, sizeof(int32_t));
  return value;
}

template <>
inline uint32_t load<uint32_t>(const void* mem) {
  uint32_t value;
  std::memcpy(&value, mem, sizeof(uint32_t));
  return value;
}

template <>
inline int64_t load<int64_t>(const void* mem) {
  int64_t value;
  std::memcpy(&value, mem, sizeof(int64_t));
  return value;
}

template <>
inline uint64_t load<uint64_t>(const void* mem) {
  uint64_t value;
  std::memcpy(&value, mem, sizeof(uint64_t));
  return value;
}

template <>
inline float load<float>(const void* mem) {
  float value;
  std::memcpy(&value, mem, sizeof(float));
  return value;
}

template <>
inline double load<double>(const void* mem) {
  double value;
  std::memcpy(&value, mem, sizeof(double));
  return value;
}

template <typename T>
inline T load(const void* mem) {
  if (sizeof(T) == 1) {
    return load<uint8_t>(mem);
  } else if (sizeof(T) == 2) {
    return load<uint16_t>(mem);
  } else if (sizeof(T) == 4) {
    return load<uint32_t>(mem);
  } else if (sizeof(T) == 8) {
    return load<uint64_t>(mem);
  } else {
    assert(false && "Invalid xe::load size");
  }
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
  int16_t value;
  std::memcpy(&value, mem, sizeof(int16_t));
  return byte_swap(value);
}
template <>
inline uint16_t load_and_swap<uint16_t>(const void* mem) {
  uint16_t value;
  std::memcpy(&value, mem, sizeof(uint16_t));
  return byte_swap(value);
}
template <>
inline int32_t load_and_swap<int32_t>(const void* mem) {
  int32_t value;
  std::memcpy(&value, mem, sizeof(int32_t));
  return byte_swap(value);
}
template <>
inline uint32_t load_and_swap<uint32_t>(const void* mem) {
  uint32_t value;
  std::memcpy(&value, mem, sizeof(uint32_t));
  return byte_swap(value);
}
template <>
inline int64_t load_and_swap<int64_t>(const void* mem) {
  int64_t value;
  std::memcpy(&value, mem, sizeof(int64_t));
  return byte_swap(value);
}
template <>
inline uint64_t load_and_swap<uint64_t>(const void* mem) {
  uint64_t value;
  std::memcpy(&value, mem, sizeof(uint64_t));
  return byte_swap(value);
}
template <>
inline float load_and_swap<float>(const void* mem) {
  float value;
  std::memcpy(&value, mem, sizeof(float));
  return byte_swap(value);
}
template <>
inline double load_and_swap<double>(const void* mem) {
  double value;
  std::memcpy(&value, mem, sizeof(double));
  return byte_swap(value);
}
template <>
inline std::string load_and_swap<std::string>(const void* mem) {
  std::string value;
  for (int i = 0;; ++i) {
    auto c =
        xe::load_and_swap<uint8_t>(reinterpret_cast<const uint8_t*>(mem) + i);
    if (!c) {
      break;
    }
    value.push_back(static_cast<char>(c));
  }
  return value;
}
template <>
inline std::u16string load_and_swap<std::u16string>(const void* mem) {
  std::u16string value;
  for (int i = 0;; ++i) {
    auto c =
        xe::load_and_swap<uint16_t>(reinterpret_cast<const uint16_t*>(mem) + i);
    if (!c) {
      break;
    }
    value.push_back(static_cast<wchar_t>(c));
  }
  return value;
}

template <typename T>
void store(void* mem, const T& value);
template <>
inline void store<int8_t>(void* mem, const int8_t& value) {
  *reinterpret_cast<int8_t*>(mem) = value;
}
template <>
inline void store<uint8_t>(void* mem, const uint8_t& value) {
  *reinterpret_cast<uint8_t*>(mem) = value;
}
template <>
inline void store<int16_t>(void* mem, const int16_t& value) {
  *reinterpret_cast<int16_t*>(mem) = value;
}
template <>
inline void store<uint16_t>(void* mem, const uint16_t& value) {
  *reinterpret_cast<uint16_t*>(mem) = value;
}
template <>
inline void store<int32_t>(void* mem, const int32_t& value) {
  *reinterpret_cast<int32_t*>(mem) = value;
}
template <>
inline void store<uint32_t>(void* mem, const uint32_t& value) {
  *reinterpret_cast<uint32_t*>(mem) = value;
}
template <>
inline void store<int64_t>(void* mem, const int64_t& value) {
  *reinterpret_cast<int64_t*>(mem) = value;
}
template <>
inline void store<uint64_t>(void* mem, const uint64_t& value) {
  *reinterpret_cast<uint64_t*>(mem) = value;
}
template <>
inline void store<float>(void* mem, const float& value) {
  *reinterpret_cast<float*>(mem) = value;
}
template <>
inline void store<double>(void* mem, const double& value) {
  *reinterpret_cast<double*>(mem) = value;
}
template <typename T>
constexpr inline void store(const void* mem, const T& value) {
  if constexpr (sizeof(T) == 1) {
    store<uint8_t>(mem, static_cast<uint8_t>(value));
  } else if constexpr (sizeof(T) == 2) {
    store<uint8_t>(mem, static_cast<uint16_t>(value));
  } else if constexpr (sizeof(T) == 4) {
    store<uint8_t>(mem, static_cast<uint32_t>(value));
  } else if constexpr (sizeof(T) == 8) {
    store<uint8_t>(mem, static_cast<uint64_t>(value));
  } else {
    static_assert("Invalid xe::store size");
  }
}

template <typename T>
void store_and_swap(void* mem, const T& value);
template <>
inline void store_and_swap<int8_t>(void* mem, const int8_t& value) {
  *reinterpret_cast<int8_t*>(mem) = value;
}
template <>
inline void store_and_swap<uint8_t>(void* mem, const uint8_t& value) {
  *reinterpret_cast<uint8_t*>(mem) = value;
}
template <>
inline void store_and_swap<int16_t>(void* mem, const int16_t& value) {
  int16_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(int16_t));
}
template <>
inline void store_and_swap<uint16_t>(void* mem, const uint16_t& value) {
  uint16_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(uint16_t));
}
template <>
inline void store_and_swap<int32_t>(void* mem, const int32_t& value) {
  int32_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(int32_t));
}
template <>
inline void store_and_swap<uint32_t>(void* mem, const uint32_t& value) {
  uint32_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(uint32_t));
}
template <>
inline void store_and_swap<int64_t>(void* mem, const int64_t& value) {
  int64_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(int64_t));
}
template <>
inline void store_and_swap<uint64_t>(void* mem, const uint64_t& value) {
  uint64_t swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(uint64_t));
}
template <>
inline void store_and_swap<float>(void* mem, const float& value) {
  float swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(float));
}
template <>
inline void store_and_swap<double>(void* mem, const double& value) {
  double swapped = byte_swap(value);
  std::memcpy(mem, &swapped, sizeof(double));
}
template <>
inline void store_and_swap<std::string_view>(void* mem,
                                             const std::string_view& value) {
  for (auto i = 0; i < value.size(); ++i) {
    xe::store_and_swap<uint8_t>(reinterpret_cast<uint8_t*>(mem) + i, value[i]);
  }
}
template <>
inline void store_and_swap<std::string>(void* mem, const std::string& value) {
  return store_and_swap<std::string_view>(mem, value);
}
template <>
inline void store_and_swap<std::u16string_view>(
    void* mem, const std::u16string_view& value) {
  for (auto i = 0; i < value.size(); ++i) {
    xe::store_and_swap<uint16_t>(reinterpret_cast<uint16_t*>(mem) + i,
                                 value[i]);
  }
}
template <>
inline void store_and_swap<std::u16string>(void* mem,
                                           const std::u16string& value) {
  return store_and_swap<std::u16string_view>(mem, value);
}

using fourcc_t = uint32_t;

// Get FourCC in host byte order
// make_fourcc('a', 'b', 'c', 'd') == 0x61626364
constexpr inline fourcc_t make_fourcc(char a, char b, char c, char d) {
  return fourcc_t((static_cast<fourcc_t>(a) << 24) |
                  (static_cast<fourcc_t>(b) << 16) |
                  (static_cast<fourcc_t>(c) << 8) | static_cast<fourcc_t>(d));
}

// Get FourCC in host byte order
// This overload requires fourcc.length() == 4
// make_fourcc("abcd") == 'abcd' == 0x61626364 for most compilers
constexpr inline fourcc_t make_fourcc(const std::string_view fourcc) {
    if (fourcc.size() != 4) {
        throw std::runtime_error("Invalid fourcc length");
    }

    // Explicitly construct the fourcc code to ensure compatibility
    return static_cast<fourcc_t>(fourcc[0] << 24) |
           (static_cast<fourcc_t>(fourcc[1]) << 16) |
           (static_cast<fourcc_t>(fourcc[2]) << 8) |
           (static_cast<fourcc_t>(fourcc[3]));
}

}  // namespace xe

#endif  // XENIA_BASE_MEMORY_H_
