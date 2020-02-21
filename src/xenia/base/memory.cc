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
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_32_unaligned(void* dest_ptr, const void* src_ptr,
                                size_t count) {
  auto dest = reinterpret_cast<uint32_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint32_t*>(src_ptr);
  __m128i shufmask =
      _mm_set_epi8(0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05,
                   0x06, 0x07, 0x00, 0x01, 0x02, 0x03);

  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
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
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = byte_swap(src[i]);
  }
}

void copy_and_swap_16_in_32_aligned(void* dest_ptr, const void* src_ptr,
                                    size_t count) {
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_load_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output =
        _mm_or_si128(_mm_slli_epi32(input, 16), _mm_srli_epi32(input, 16));
    _mm_store_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
  }
  for (; i < count; ++i) {  // handle residual elements
    dest[i] = (src[i] >> 16) | (src[i] << 16);
  }
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
