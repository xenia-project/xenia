/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_ARCH_ARM64
#include <arm_neon.h>
#endif

#include <algorithm>

DEFINE_bool(
    writable_executable_memory, true,
    "Allow mapping memory with both write and execute access, for simulating "
    "behavior on platforms where that's not supported",
    "Memory");

namespace xe {
namespace memory {

bool IsWritableExecutableMemoryPreferred() {
  return IsWritableExecutableMemorySupported() &&
         cvars::writable_executable_memory;
}

using xe::swcache::CacheLine;

static constexpr unsigned NUM_CACHELINES_IN_PAGE = 4096 / sizeof(CacheLine);

#if defined(__clang__)
XE_FORCEINLINE
static void mvdir64b(void* to, const void* from) {
  __asm__("movdir64b %1, %0" : : "r"(to), "m"(*(char*)from) : "memory");
}
#define _movdir64b mvdir64b
#endif
XE_FORCEINLINE
static void XeCopy16384StreamingAVX(CacheLine* XE_RESTRICT to,
                                    CacheLine* XE_RESTRICT from) {
  uint32_t num_lines_for_8k = 4096 / XE_HOST_CACHE_LINE_SIZE;

  CacheLine* dest1 = to;
  CacheLine* src1 = from;

  CacheLine* dest2 = to + NUM_CACHELINES_IN_PAGE;
  CacheLine* src2 = from + NUM_CACHELINES_IN_PAGE;

  CacheLine* dest3 = to + (NUM_CACHELINES_IN_PAGE * 2);
  CacheLine* src3 = from + (NUM_CACHELINES_IN_PAGE * 2);

  CacheLine* dest4 = to + (NUM_CACHELINES_IN_PAGE * 3);
  CacheLine* src4 = from + (NUM_CACHELINES_IN_PAGE * 3);

  for (uint32_t i = 0; i < num_lines_for_8k; ++i) {
    xe::swcache::CacheLine line0, line1, line2, line3;

    xe::swcache::ReadLine(&line0, src1 + i);
    xe::swcache::ReadLine(&line1, src2 + i);
    xe::swcache::ReadLine(&line2, src3 + i);
    xe::swcache::ReadLine(&line3, src4 + i);
    XE_MSVC_REORDER_BARRIER();
    xe::swcache::WriteLineNT(dest1 + i, &line0);
    xe::swcache::WriteLineNT(dest2 + i, &line1);

    xe::swcache::WriteLineNT(dest3 + i, &line2);
    xe::swcache::WriteLineNT(dest4 + i, &line3);
  }
  XE_MSVC_REORDER_BARRIER();
}
XE_FORCEINLINE
static void XeCopy16384Movdir64M(CacheLine* XE_RESTRICT to,
                                 CacheLine* XE_RESTRICT from) {
  uint32_t num_lines_for_8k = 4096 / XE_HOST_CACHE_LINE_SIZE;

  CacheLine* dest1 = to;
  CacheLine* src1 = from;

  CacheLine* dest2 = to + NUM_CACHELINES_IN_PAGE;
  CacheLine* src2 = from + NUM_CACHELINES_IN_PAGE;

  CacheLine* dest3 = to + (NUM_CACHELINES_IN_PAGE * 2);
  CacheLine* src3 = from + (NUM_CACHELINES_IN_PAGE * 2);

  CacheLine* dest4 = to + (NUM_CACHELINES_IN_PAGE * 3);
  CacheLine* src4 = from + (NUM_CACHELINES_IN_PAGE * 3);
  for (uint32_t i = 0; i < num_lines_for_8k; ++i) {
    _movdir64b(dest1 + i, src1 + i);
    _movdir64b(dest2 + i, src2 + i);
    _movdir64b(dest3 + i, src3 + i);
    _movdir64b(dest4 + i, src4 + i);
  }
  XE_MSVC_REORDER_BARRIER();
}
using VastCpyDispatch = void (*)(CacheLine* XE_RESTRICT physaddr,
                                 CacheLine* XE_RESTRICT rdmapping,
                                 uint32_t written_length);
static void vastcpy_impl_avx(CacheLine* XE_RESTRICT physaddr,
                             CacheLine* XE_RESTRICT rdmapping,
                             uint32_t written_length) {
  static constexpr unsigned NUM_LINES_FOR_16K = 16384 / XE_HOST_CACHE_LINE_SIZE;

  while (written_length >= 16384) {
    XeCopy16384StreamingAVX(physaddr, rdmapping);

    physaddr += NUM_LINES_FOR_16K;
    rdmapping += NUM_LINES_FOR_16K;

    written_length -= 16384;
  }

  if (!written_length) {
    return;
  }
  uint32_t num_written_lines = written_length / XE_HOST_CACHE_LINE_SIZE;

  uint32_t i = 0;

  for (; i + 1 < num_written_lines; i += 2) {
    xe::swcache::CacheLine line0, line1;

    xe::swcache::ReadLine(&line0, rdmapping + i);

    xe::swcache::ReadLine(&line1, rdmapping + i + 1);
    XE_MSVC_REORDER_BARRIER();
    xe::swcache::WriteLineNT(physaddr + i, &line0);
    xe::swcache::WriteLineNT(physaddr + i + 1, &line1);
  }

  if (i < num_written_lines) {
    xe::swcache::CacheLine line0;

    xe::swcache::ReadLine(&line0, rdmapping + i);
    xe::swcache::WriteLineNT(physaddr + i, &line0);
  }
}

static void vastcpy_impl_movdir64m(CacheLine* XE_RESTRICT physaddr,
                                   CacheLine* XE_RESTRICT rdmapping,
                                   uint32_t written_length) {
  static constexpr unsigned NUM_LINES_FOR_16K = 16384 / XE_HOST_CACHE_LINE_SIZE;

  while (written_length >= 16384) {
    XeCopy16384Movdir64M(physaddr, rdmapping);

    physaddr += NUM_LINES_FOR_16K;
    rdmapping += NUM_LINES_FOR_16K;

    written_length -= 16384;
  }

  if (!written_length) {
    return;
  }
  uint32_t num_written_lines = written_length / XE_HOST_CACHE_LINE_SIZE;

  uint32_t i = 0;

  for (; i + 1 < num_written_lines; i += 2) {
    _movdir64b(physaddr + i, rdmapping + i);
    _movdir64b(physaddr + i + 1, rdmapping + i + 1);
  }

  if (i < num_written_lines) {
    _movdir64b(physaddr + i, rdmapping + i);
  }
}
static void vastcpy_impl_repmovs(CacheLine* XE_RESTRICT physaddr,
                                 CacheLine* XE_RESTRICT rdmapping,
                                 uint32_t written_length) {
  __movsq((unsigned long long*)physaddr, (unsigned long long*)rdmapping,
          written_length / 8);
}
XE_COLD
static void first_vastcpy(CacheLine* XE_RESTRICT physaddr,
                          CacheLine* XE_RESTRICT rdmapping,
                          uint32_t written_length);

static VastCpyDispatch vastcpy_dispatch = first_vastcpy;

XE_COLD
static void first_vastcpy(CacheLine* XE_RESTRICT physaddr,
                          CacheLine* XE_RESTRICT rdmapping,
                          uint32_t written_length) {
  VastCpyDispatch dispatch_to_use = nullptr;
  if (amd64::GetFeatureFlags() & amd64::kX64EmitMovdir64M) {
    XELOGI("Selecting MOVDIR64M vastcpy.");
    dispatch_to_use = vastcpy_impl_movdir64m;
  } else if (amd64::GetFeatureFlags() & amd64::kX64FastRepMovs) {
    XELOGI("Selecting rep movs vastcpy.");
    dispatch_to_use = vastcpy_impl_repmovs;
  } else {
    XELOGI("Selecting generic AVX vastcpy.");
    dispatch_to_use = vastcpy_impl_avx;
  }

  vastcpy_dispatch =
      dispatch_to_use;  // all future calls will go through our selected path
  return vastcpy_dispatch(physaddr, rdmapping, written_length);
}

XE_NOINLINE
void vastcpy(uint8_t* XE_RESTRICT physaddr, uint8_t* XE_RESTRICT rdmapping,
             uint32_t written_length) {
  return vastcpy_dispatch((CacheLine*)physaddr, (CacheLine*)rdmapping,
                          written_length);
}
}  // namespace memory

// TODO(benvanik): fancy AVX versions.
// https://github.com/gnuradio/volk/blob/master/kernels/volk/volk_16u_byteswap.h
// https://github.com/gnuradio/volk/blob/master/kernels/volk/volk_32u_byteswap.h
// https://github.com/gnuradio/volk/blob/master/kernels/volk/volk_64u_byteswap.h
// Original links:
// https://gnuradio.org/redmine/projects/gnuradio/repository/revisions/cb32b70b79f430456208a2cd521d028e0ece5d5b/entry/volk/kernels/volk/volk_16u_byteswap.h
// https://gnuradio.org/redmine/projects/gnuradio/repository/revisions/f2bc76cc65ffba51a141950f98e75364e49df874/entry/volk/kernels/volk/volk_32u_byteswap.h
// https://gnuradio.org/redmine/projects/gnuradio/repository/revisions/2c4c371885c31222362f70a1cd714415d1398021/entry/volk/kernels/volk/volk_64u_byteswap.h

void copy_128_aligned(void* dest, const void* src, size_t count) {
  std::memcpy(dest, src, count * 16);
}

#if XE_ARCH_AMD64

// This works around a GCC bug
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100801
// TODO(Joel Linn): Remove this when fixed GCC versions are common place.
#if XE_COMPILER_GNUC
#define XE_WORKAROUND_CONSTANT_RETURN_IF(x) \
  if (__builtin_constant_p(x) && (x)) return;
#else
#define XE_WORKAROUND_CONSTANT_RETURN_IF(x)
#endif
void copy_and_swap_16_aligned(void* dest_ptr, const void* src_ptr,
                              size_t count) {
  assert_zero(reinterpret_cast<uintptr_t>(dest_ptr) & 0xF);
  assert_zero(reinterpret_cast<uintptr_t>(src_ptr) & 0xF);

  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0E, 0x0F, 0x0C, 0x0D, 0x0A, 0x0B, 0x08, 0x09, 0x06, 0x07,
                   0x04, 0x05, 0x02, 0x03, 0x00, 0x01);

  size_t i = 0;
  for (i = 0; i + 8 <= count; i += 8) {
    __m128i input = _mm_load_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_store_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 8 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_16_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0E, 0x0F, 0x0C, 0x0D, 0x0A, 0x0B, 0x08, 0x09, 0x06, 0x07,
                   0x04, 0x05, 0x02, 0x03, 0x00, 0x01);

  size_t i;
  for (i = 0; i + 8 <= count; i += 8) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 8 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_32_aligned(void* dest_ptr, const void* src_ptr,
                              size_t count) {
  assert_zero(reinterpret_cast<uintptr_t>(dest_ptr) & 0xF);
  assert_zero(reinterpret_cast<uintptr_t>(src_ptr) & 0xF);

  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05,
                   0x06, 0x07, 0x00, 0x01, 0x02, 0x03);

  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_load_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_store_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 4 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_32_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  size_t i;
  // chrispy: this optimization mightt backfire if our unaligned load spans two
  // cachelines... which it probably will
  if (amd64::GetFeatureFlags() & amd64::kX64EmitAVX2) {
    __m256i shufmask = _mm256_set_epi8(
        0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05, 0x06, 0x07,
        0x00, 0x01, 0x02, 0x03, 0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B,
        0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03);
    // with vpshufb being a 0.5 through instruction, it makes the most sense to
    // double up on our iters
    for (i = 0; i + 16 <= count; i += 16) {
      __m256i input1 =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&src[i]));
      __m256i input2 =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&src[i + 8]));

      __m256i output1 = _mm256_shuffle_epi8(input1, shufmask);
      __m256i output2 = _mm256_shuffle_epi8(input2, shufmask);
	  //chrispy: todo, benchmark this w/ and w/out these prefetches here on multiple machines
	  //finding a good distance for prefetchw in particular is probably important
	  //for when we're writing across 2 cachelines
	  #if 0
      if (i + 48 <= count) {
        swcache::PrefetchNTA(&src[i + 32]);
        if (amd64::GetFeatureFlags() & amd64::kX64EmitPrefetchW) {
          swcache::PrefetchW(&dest[i + 32]);
        }
      }
	  #endif
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dest[i]), output1);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dest[i + 8]), output2);
    }
    if (i + 8 <= count) {
      __m256i input =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&src[i]));
      __m256i output = _mm256_shuffle_epi8(input, shufmask);
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(&dest[i]), output);
      i += 8;
    }
    if (i + 4 <= count) {
      __m128i input =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
      __m128i output =
          _mm_shuffle_epi8(input, _mm256_castsi256_si128(shufmask));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
      i += 4;
    }
  } else {
    __m128i shufmask =
        _mm_set_epi8(0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05,
                     0x06, 0x07, 0x00, 0x01, 0x02, 0x03);

    for (i = 0; i + 4 <= count; i += 4) {
      __m128i input =
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
      __m128i output = _mm_shuffle_epi8(input, shufmask);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
    }
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 4 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_64_aligned(void* dest_ptr, const void* src_ptr,
                              size_t count) {
  assert_zero(reinterpret_cast<uintptr_t>(dest_ptr) & 0xF);
  assert_zero(reinterpret_cast<uintptr_t>(src_ptr) & 0xF);

  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x00, 0x01,
                   0x02, 0x03, 0x04, 0x05, 0x06, 0x07);

  size_t i;
  for (i = 0; i + 2 <= count; i += 2) {
    __m128i input = _mm_load_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_store_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 2 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_64_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x00, 0x01,
                   0x02, 0x03, 0x04, 0x05, 0x06, 0x07);

  size_t i;
  for (i = 0; i + 2 <= count; i += 2) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 2 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_16_in_32_aligned(void* dest_ptr, const void* src_ptr,
                                    size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_load_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output =
        _mm_or_si128(_mm_slli_epi32(input, 16), _mm_srli_epi32(input, 16));
    _mm_store_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 4 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = (src[i] >> 16) | (src[i] << 16);
  }
}

void copy_and_swap_16_in_32_unaligned(void* dest_ptr, const void* src_ptr,
                                      size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output =
        _mm_or_si128(_mm_slli_epi32(input, 16), _mm_srli_epi32(input, 16));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  XE_WORKAROUND_CONSTANT_RETURN_IF(count % 4 == 0);
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = (src[i] >> 16) | (src[i] << 16);
  }
}

#elif XE_ARCH_ARM64

// Although NEON offers vector rev instructions (like vrev32q_u8), they are
// slower in benchmarks. Also, using uint8x16xN_t wasn't any faster in the
// benchmarks, hence we use just use one SIMD register to minimize residual
// processing.

void copy_and_swap_16_aligned(void* dst_ptr, const void* src_ptr,
                              size_t count) {
  copy_and_swap_16_unaligned(dst_ptr, src_ptr, count);
}

void copy_and_swap_16_unaligned(void* dst_ptr, const void* src_ptr,
                                size_t count) {
  auto dst = reinterpret_cast<uint8_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint8_t*>(src_ptr);

  const uint8x16_t tbl_idx =
      vcombine_u8(vcreate_u8(UINT64_C(0x0607040502030001)),
                  vcreate_u8(UINT64_C(0x0E0F0C0D0A0B0809)));

  while (count >= 8) {
    uint8x16_t data = vld1q_u8(src);
    data = vqtbl1q_u8(data, tbl_idx);
    vst1q_u8(dst, data);

    count -= 8;
    // These pointer increments will be combined with the load/stores (ldr/str)
    // into single instructions (at least by clang)
    dst += 16;
    src += 16;
  }

  while (count > 0) {
    store_and_swap<uint16_t>(dst, load<uint16_t>(src));

    count--;
    dst += 2;
    src += 2;
  }
}

void copy_and_swap_32_aligned(void* dst, const void* src, size_t count) {
  copy_and_swap_32_unaligned(dst, src, count);
}

void copy_and_swap_32_unaligned(void* dst_ptr, const void* src_ptr,
                                size_t count) {
  auto dst = reinterpret_cast<uint8_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint8_t*>(src_ptr);

  const uint8x16_t tbl_idx =
      vcombine_u8(vcreate_u8(UINT64_C(0x405060700010203)),
                  vcreate_u8(UINT64_C(0x0C0D0E0F08090A0B)));

  while (count >= 4) {
    uint8x16_t data = vld1q_u8(src);
    data = vqtbl1q_u8(data, tbl_idx);
    vst1q_u8(dst, data);

    count -= 4;
    dst += 16;
    src += 16;
  }

  while (count > 0) {
    store_and_swap<uint32_t>(dst, load<uint32_t>(src));

    count--;
    dst += 4;
    src += 4;
  }
}

void copy_and_swap_64_aligned(void* dst, const void* src, size_t count) {
  copy_and_swap_64_unaligned(dst, src, count);
}

void copy_and_swap_64_unaligned(void* dst_ptr, const void* src_ptr,
                                size_t count) {
  auto dst = reinterpret_cast<uint8_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint8_t*>(src_ptr);

  const uint8x16_t tbl_idx =
      vcombine_u8(vcreate_u8(UINT64_C(0x0001020304050607)),
                  vcreate_u8(UINT64_C(0x08090A0B0C0D0E0F)));

  while (count >= 2) {
    uint8x16_t data = vld1q_u8(src);
    data = vqtbl1q_u8(data, tbl_idx);
    vst1q_u8(dst, data);

    count -= 2;
    dst += 16;
    src += 16;
  }

  while (count > 0) {
    store_and_swap<uint64_t>(dst, load<uint64_t>(src));

    count--;
    dst += 8;
    src += 8;
  }
}

void copy_and_swap_16_in_32_aligned(void* dst, const void* src, size_t count) {
  return copy_and_swap_16_in_32_unaligned(dst, src, count);
}

void copy_and_swap_16_in_32_unaligned(void* dst_ptr, const void* src_ptr,
                                      size_t count) {
  auto dst = reinterpret_cast<uint16_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  while (count > 0) {
    uint16_t word0 = *src++;
    uint16_t word1 = *src++;
    *dst++ = word1;
    *dst++ = word0;

    count--;
  }
}

#else

// Generic routines.
void copy_and_swap_16_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_16_unaligned(dest, src, count);
}

void copy_and_swap_16_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_32_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_32_unaligned(dest, src, count);
}

void copy_and_swap_32_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_64_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_64_unaligned(dest, src, count);
}

void copy_and_swap_64_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_16_in_32_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_16_in_32_unaligned(dest, src, count);
}

void copy_and_swap_16_in_32_unaligned(void* dst_ptr, const void* src_ptr,
                                      size_t count) {
  auto dst = reinterpret_cast<uint16_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  while (count > 0) {
    uint16_t word0 = *src++;
    uint16_t word1 = *src++;
    *dst++ = word1;
    *dst++ = word0;

    count--;
  }
}

#endif

}  // namespace xe
