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


#include "xenia/base/byte_order.h"


namespace xe {
namespace memory {

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
// must be divisible by cache line size
XE_NOINLINE
void vastcpy(uint8_t* XE_RESTRICT physaddr, uint8_t* XE_RESTRICT rdmapping,
             uint32_t written_length);

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
  *reinterpret_cast<int16_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint16_t>(void* mem, const uint16_t& value) {
  *reinterpret_cast<uint16_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<int32_t>(void* mem, const int32_t& value) {
  *reinterpret_cast<int32_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint32_t>(void* mem, const uint32_t& value) {
  *reinterpret_cast<uint32_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<int64_t>(void* mem, const int64_t& value) {
  *reinterpret_cast<int64_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<uint64_t>(void* mem, const uint64_t& value) {
  *reinterpret_cast<uint64_t*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<float>(void* mem, const float& value) {
  *reinterpret_cast<float*>(mem) = byte_swap(value);
}
template <>
inline void store_and_swap<double>(void* mem, const double& value) {
  *reinterpret_cast<double*>(mem) = byte_swap(value);
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
  if (fourcc.length() != 4) {
    throw std::runtime_error("Invalid fourcc length");
  }
  return make_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
}

// chrispy::todo:use for command stream vector, resize happens a ton and has to
// call memset
template <size_t sz>
class FixedVMemVector {
  static_assert((sz & 65535) == 0,
                "Always give fixed_vmem_vector a size divisible by 65536 to "
                "avoid wasting memory on windows");

  uint8_t* data_;
  size_t nbytes_;

 public:
  FixedVMemVector()
      : data_((uint8_t*)memory::AllocFixed(
            nullptr, sz, memory::AllocationType::kReserveCommit,
            memory::PageAccess::kReadWrite)),
        nbytes_(0) {}
  ~FixedVMemVector() {
    if (data_) {
      memory::DeallocFixed(data_, sz, memory::DeallocationType::kRelease);
      data_ = nullptr;
    }
    nbytes_ = 0;
  }

  uint8_t* data() const { return data_; }
  size_t size() const { return nbytes_; }

  void resize(size_t newsize) {
    nbytes_ = newsize;
    xenia_assert(newsize < sz);
  }
  size_t alloc() const { return sz; }

  void clear() {
    resize(0);  // todo:maybe zero out
  }
  void reserve(size_t size) { xenia_assert(size < sz); }
};
// software prefetches/cache operations
namespace swcache {
/*
        warning, prefetchw's current behavior is not consistent across msvc and
   clang, for clang it will only compile to prefetchw if the set architecture
   supports it, for msvc however it will unconditionally compile to prefetchw!
        so prefetchw support is still in process


        only use these if you're absolutely certain you know what you're doing;
   you can easily tank performance through misuse CPUS have excellent automatic
   prefetchers that can predict patterns, but in situations where memory
   accesses are super unpredictable and follow no pattern you can make use of
   them

        another scenario where it can be handy is when crossing page boundaries,
   as many automatic prefetchers do not allow their streams to cross pages (no
   idea what this means for huge pages)

        I believe software prefetches do not kick off an automatic prefetcher
   stream, so you can't just prefetch one line of the data you're about to
   access and be fine, you need to go all the way

        prefetchnta is implementation dependent, and that makes its use a bit
   limited. For intel cpus, i believe it only prefetches the line into one way
   of the L3

        for amd cpus, it marks the line as requiring immediate eviction, the
   next time an entry is needed in the set it resides in it will be evicted. ms
   does dumb shit for memcpy, like looping over the contents of the source
   buffer and doing prefetchnta on them, likely evicting some of the data they
   just prefetched by the end of the buffer, and probably messing up data that
   was already in the cache


        another warning for these: this bypasses what i think is called
   "critical word load", the data will always become available starting from the
   very beginning of the line instead of from the piece that is needed

        L1I cache is not prefetchable, however likely all cpus can fulfill
   requests for the L1I from L2, so prefetchL2 on instructions should be fine

        todo: clwb, clflush
*/
#if XE_COMPILER_HAS_GNU_EXTENSIONS == 1

XE_FORCEINLINE
static void PrefetchW(const void* addr) { __builtin_prefetch(addr, 1, 0); }
XE_FORCEINLINE

static void PrefetchNTA(const void* addr) { __builtin_prefetch(addr, 0, 0); }
XE_FORCEINLINE

static void PrefetchL3(const void* addr) { __builtin_prefetch(addr, 0, 1); }
XE_FORCEINLINE

static void PrefetchL2(const void* addr) { __builtin_prefetch(addr, 0, 2); }
XE_FORCEINLINE

static void PrefetchL1(const void* addr) { __builtin_prefetch(addr, 0, 3); }
#elif XE_ARCH_AMD64 == 1 && XE_COMPILER_MSVC == 1
XE_FORCEINLINE
static void PrefetchW(const void* addr) { _m_prefetchw(addr); }

XE_FORCEINLINE
static void PrefetchNTA(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_NTA);
}
XE_FORCEINLINE

static void PrefetchL3(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T2);
}
XE_FORCEINLINE

static void PrefetchL2(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T1);
}
XE_FORCEINLINE

static void PrefetchL1(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T0);
}

#else
XE_FORCEINLINE
static void PrefetchW(const void* addr) {}

XE_FORCEINLINE
static void PrefetchNTA(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL3(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL2(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL1(const void* addr) {}

#endif

enum class PrefetchTag { Write, Nontemporal, Level3, Level2, Level1 };

template <PrefetchTag tag>
static void Prefetch(const void* addr) {
  xenia_assert(false && "Unknown tag");
}

template <>
XE_MAYBE_UNUSED void Prefetch<PrefetchTag::Write>(const void* addr) {
  PrefetchW(addr);
}
template <>
XE_MAYBE_UNUSED void Prefetch<PrefetchTag::Nontemporal>(const void* addr) {
  PrefetchNTA(addr);
}
template <>
XE_MAYBE_UNUSED void Prefetch<PrefetchTag::Level3>(const void* addr) {
  PrefetchL3(addr);
}
template <>
XE_MAYBE_UNUSED void Prefetch<PrefetchTag::Level2>(const void* addr) {
  PrefetchL2(addr);
}
template <>
XE_MAYBE_UNUSED void Prefetch<PrefetchTag::Level1>(const void* addr) {
  PrefetchL1(addr);
}
// todo: does aarch64 have streaming stores/loads?

/*
        non-temporal stores/loads

        the stores allow cacheable memory to behave like write-combining memory.
        on the first nt store to a line, an intermediate buffer will be
   allocated by the cpu for stores that come after. once the entire contents of
   the line have been written the intermediate buffer will be transmitted to
   memory

        the written line will not be cached and if it is in the cache it will be
   invalidated from all levels of the hierarchy

        the cpu in this case does not have to read line from memory when we
   first write to it if it is not anywhere in the cache, so we use half the
   memory bandwidth using these stores

        non-temporal loads are... loads, but they dont use the cache. you need
   to manually insert memory barriers (_ReadWriteBarrier, ReadBarrier, etc, do
   not use any barriers that generate actual code) if on msvc to prevent it from
   moving the load of the data to just before the use of the data (immediately
   requiring the memory to be available = big stall)



*/

#if XE_COMPILER_MSVC == 1 && XE_COMPILER_CLANG_CL == 0
#define XE_MSVC_REORDER_BARRIER _ReadWriteBarrier

#else
// if the compiler actually has pipelining for instructions we dont need a
// barrier
#define XE_MSVC_REORDER_BARRIER() static_cast<void>(0)
#endif
#if XE_ARCH_AMD64 == 1
union alignas(XE_HOST_CACHE_LINE_SIZE) CacheLine {
  struct {
    __m256 low32;
    __m256 high32;
  };
  struct {
    __m128i xmms[4];
  };
  float floats[XE_HOST_CACHE_LINE_SIZE / sizeof(float)];
};
XE_FORCEINLINE
static void WriteLineNT(CacheLine* XE_RESTRICT destination,
                        const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(destination) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_stream_ps(&destination->floats[0], low);
  _mm256_stream_ps(&destination->floats[8], high);
}

XE_FORCEINLINE
static void ReadLineNT(CacheLine* XE_RESTRICT destination,
                       const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(source) & 63ULL) == 0);

  __m128i first = _mm_stream_load_si128(&source->xmms[0]);
  __m128i second = _mm_stream_load_si128(&source->xmms[1]);
  __m128i third = _mm_stream_load_si128(&source->xmms[2]);
  __m128i fourth = _mm_stream_load_si128(&source->xmms[3]);

  destination->xmms[0] = first;
  destination->xmms[1] = second;
  destination->xmms[2] = third;
  destination->xmms[3] = fourth;
}
XE_FORCEINLINE
static void ReadLine(CacheLine* XE_RESTRICT destination,
                     const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(source) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_storeu_ps(&destination->floats[0], low);
  _mm256_storeu_ps(&destination->floats[8], high);
}
XE_FORCEINLINE
static void WriteLine(CacheLine* XE_RESTRICT destination,
                      const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(destination) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_storeu_ps(&destination->floats[0], low);
  _mm256_storeu_ps(&destination->floats[8], high);
}

XE_FORCEINLINE
static void WriteFence() { _mm_sfence(); }
XE_FORCEINLINE
static void ReadFence() { _mm_lfence(); }
XE_FORCEINLINE
static void ReadWriteFence() { _mm_mfence(); }

#else
union alignas(XE_HOST_CACHE_LINE_SIZE) CacheLine {
  uint8_t bvals[XE_HOST_CACHE_LINE_SIZE];
};
XE_FORCEINLINE
static void WriteLineNT(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}

XE_FORCEINLINE
static void ReadLineNT(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}
XE_FORCEINLINE
static void WriteLine(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}
XE_FORCEINLINE
static void ReadLine(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}

XE_FORCEINLINE
static void WriteFence() {}
XE_FORCEINLINE
static void ReadFence() {}
XE_FORCEINLINE
static void ReadWriteFence() {}
#endif
}  // namespace swcache

template <unsigned Size>
static void smallcpy_const(void* destination, const void* source) {
#if XE_ARCH_AMD64 == 1 && XE_COMPILER_MSVC == 1
  if constexpr ((Size & 7) == 0) {
    __movsq((unsigned long long*)destination, (const unsigned long long*)source,
            Size / 8);
  } else if constexpr ((Size & 3) == 0) {
    __movsd((unsigned long*)destination, (const unsigned long*)source,
            Size / 4);
    // dont even bother with movsw, i think the operand size override prefix
    // slows it down
  } else {
    __movsb((unsigned char*)destination, (const unsigned char*)source, Size);
  }
#else
  memcpy(destination, source, Size);
#endif
}
template <unsigned Size>
static void smallset_const(void* destination, unsigned char fill_value) {
#if XE_ARCH_AMD64 == 1 && XE_COMPILER_MSVC == 1
  if constexpr ((Size & 7) == 0) {
    unsigned long long fill =
        static_cast<unsigned long long>(fill_value) * 0x0101010101010101ULL;

    __stosq((unsigned long long*)destination, fill, Size / 8);
  } else if constexpr ((Size & 3) == 0) {
    static constexpr unsigned long fill =
        static_cast<unsigned long>(fill_value) * 0x01010101U;
    __stosd((unsigned long*)destination, fill, Size / 4);
    // dont even bother with movsw, i think the operand size override prefix
    // slows it down
  } else {
    __stosb((unsigned char*)destination, fill_value, Size);
  }
#else
  memset(destination, fill_value, Size);
#endif
}

}  // namespace xe

#endif  // XENIA_BASE_MEMORY_H_
