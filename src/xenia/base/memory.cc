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
  __m128i shufmask =
      _mm_set_epi8(0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B, 0x04, 0x05,
                   0x06, 0x07, 0x00, 0x01, 0x02, 0x03);

  size_t i;
  for (i = 0; i + 4 <= count; i += 4) {
    __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
    __m128i output = _mm_shuffle_epi8(input, shufmask);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&dest[i]), output);
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
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
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
  auto dest = reinterpret_cast<uint64_t*>(dest_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
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
  auto dst = reinterpret_cast<uint64_t*>(dst_ptr);
  auto src = reinterpret_cast<const uint64_t*>(src_ptr);
  for (size_t i = 0; i < count; ++i) {
    dst[i] = (src[i] >> 16) | (src[i] << 16);
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
