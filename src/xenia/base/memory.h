/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MEMORY_H_
#define XENIA_BASE_MEMORY_H_

#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"

namespace xe {
namespace memory {

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

typedef void* FileMappingHandle;

FileMappingHandle CreateFileMappingHandle(std::wstring path, size_t length,
                                          PageAccess access, bool commit);
void CloseFileMappingHandle(FileMappingHandle handle);
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
inline T load(const void* mem) {
  if (sizeof(T) == 1) {
    return static_cast<T>(load<uint8_t>(mem));
  } else if (sizeof(T) == 2) {
    return static_cast<T>(load<uint16_t>(mem));
  } else if (sizeof(T) == 4) {
    return static_cast<T>(load<uint32_t>(mem));
  } else if (sizeof(T) == 8) {
    return static_cast<T>(load<uint64_t>(mem));
  } else {
    assert_always("Invalid xe::load size");
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
        xe::load_and_swap<uint8_t>(reinterpret_cast<const uint8_t*>(mem) + i);
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
inline void store(const void* mem, T value) {
  if (sizeof(T) == 1) {
    store<uint8_t>(mem, static_cast<uint8_t>(value));
  } else if (sizeof(T) == 2) {
    store<uint8_t>(mem, static_cast<uint16_t>(value));
  } else if (sizeof(T) == 4) {
    store<uint8_t>(mem, static_cast<uint32_t>(value));
  } else if (sizeof(T) == 8) {
    store<uint8_t>(mem, static_cast<uint64_t>(value));
  } else {
    assert_always("Invalid xe::store size");
  }
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
template <>
inline void store_and_swap<std::string>(void* mem, std::string value) {
  for (auto i = 0; i < value.size(); ++i) {
    xe::store_and_swap<uint8_t>(reinterpret_cast<uint8_t*>(mem) + i, value[i]);
  }
}
template <>
inline void store_and_swap<std::wstring>(void* mem, std::wstring value) {
  for (auto i = 0; i < value.size(); ++i) {
    xe::store_and_swap<uint16_t>(reinterpret_cast<uint16_t*>(mem) + i,
                                 value[i]);
  }
}

}  // namespace xe

#endif  // XENIA_BASE_MEMORY_H_
