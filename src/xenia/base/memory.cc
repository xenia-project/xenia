/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/memory.h"
#include "xenia/base/platform.h"

#include <algorithm>

namespace xe {

// TODO(benvanik): fancy AVX versions.
// http://gnuradio.org/redmine/projects/gnuradio/repository/revisions/cb32b70b79f430456208a2cd521d028e0ece5d5b/entry/volk/kernels/volk/volk_16u_byteswap.h
// http://gnuradio.org/redmine/projects/gnuradio/repository/revisions/f2bc76cc65ffba51a141950f98e75364e49df874/entry/volk/kernels/volk/volk_32u_byteswap.h
// http://gnuradio.org/redmine/projects/gnuradio/repository/revisions/2c4c371885c31222362f70a1cd714415d1398021/entry/volk/kernels/volk/volk_64u_byteswap.h

void copy_128_aligned(void* dest, const void* src, size_t count) {
  std::memcpy(dest, src, count * 16);
}

#if XE_ARCH_AMD64
void copy_and_swap_16_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_16_unaligned(dest, src, count);
}

void copy_and_swap_16_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint16_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint16_t*>(src_ptr);
  size_t i;
  for (i = 0; i + 8 <= count; i += 8) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output =
        _mm_or_si128(_mm_slli_epi16(input, 8), _mm_srli_epi16(input, 8));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
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
  __m128i byte2mask = _mm_set1_epi32(0x00FF0000);
  __m128i byte3mask = _mm_set1_epi32(0x0000FF00);
  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    // Do the four shifts.
    __m128i byte1 = _mm_slli_epi32(input, 24);
    __m128i byte2 = _mm_slli_epi32(input, 8);
    __m128i byte3 = _mm_srli_epi32(input, 8);
    __m128i byte4 = _mm_srli_epi32(input, 24);
    // OR bytes together.
    __m128i output = _mm_or_si128(byte1, byte4);
    byte2 = _mm_and_si128(byte2, byte2mask);
    output = _mm_or_si128(output, byte2);
    byte3 = _mm_and_si128(byte3, byte3mask);
    output = _mm_or_si128(output, byte3);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
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
  __m128i byte2mask = _mm_set1_epi32(0x00FF0000);
  __m128i byte3mask = _mm_set1_epi32(0x0000FF00);
  size_t i;
  for (i = 0; i + 2 <= count; i += 2) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    // Do the four shifts.
    __m128i byte1 = _mm_slli_epi32(input, 24);
    __m128i byte2 = _mm_slli_epi32(input, 8);
    __m128i byte3 = _mm_srli_epi32(input, 8);
    __m128i byte4 = _mm_srli_epi32(input, 24);
    // OR bytes together.
    __m128i output = _mm_or_si128(byte1, byte4);
    byte2 = _mm_and_si128(byte2, byte2mask);
    output = _mm_or_si128(output, byte2);
    byte3 = _mm_and_si128(byte3, byte3mask);
    output = _mm_or_si128(output, byte3);
    // Reorder the two words.
    output = _mm_shuffle_epi32(output, _MM_SHUFFLE(2, 3, 0, 1));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_16_in_32_aligned(void* dest, const void* src, size_t count) {
  return copy_and_swap_16_in_32_unaligned(dest, src, count);
}

void copy_and_swap_16_in_32_unaligned(void* dest_ptr, const void* src_ptr,
                                      size_t count) {
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output =
        _mm_or_si128(_mm_slli_epi32(input, 16), _mm_srli_epi32(input, 16));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = (src[i] >> 16) | (src[i] << 16);
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

void copy_and_swap_16_in_32_unaligned(void* dest_ptr, const void* src_ptr,
                                      size_t count) {
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    dest[i] = (src[i] >> 16) | (src[i] << 16);
  }
}
#endif

}  // namespace xe
