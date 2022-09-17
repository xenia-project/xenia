/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_PRIMITIVE_PROCESSOR_H_
#define XENIA_GPU_PRIMITIVE_PROCESSOR_H_

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/base/mutex.h"
#include "xenia/base/platform.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/shared_memory.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

#if XE_ARCH_AMD64
// 128-bit SSSE3-level (SSE2+ for integer comparison, SSSE3 for pshufb) or AVX
// (256-bit AVX only got integer operations such as comparison in AVX2, which is
// above the minimum requirements of Xenia).
#include <tmmintrin.h>
#define XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE 16
#elif XE_ARCH_ARM64
#include <arm_neon.h>
#define XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE 16
#else
#define XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE 0
#endif  // XE_ARCH

// The idea behind this config variable is to force both indirection without
// primitive reset and pre-masking / pre-swapping with primitive reset,
// therefore this is supposed to be checked only by the host if it supports
// indirection. It's pretty pointless to do only half of this on backends that
// support full 32-bit indices unconditionally.
DECLARE_bool(ignore_32bit_vertex_index_support);

namespace xe {
namespace gpu {

// Normalizes primitive data in various ways for use with Direct3D 12 and Vulkan
// (down to its minimum requirements plus the portability subset).
//
// This solves various issues:
// - Triangle fans not supported on Direct3D 10+ and the Vulkan portability
//   subset.
//   - Converts to triangle lists, both with and without primitive reset.
// - Line loops are not supported on Direct3D 12 or Vulkan.
//   - Converts to line strips.
// - Quads not reproducible with line lists with adjacency without geometry
//   shaders (some Vulkan implementations), as well as being hard to debug in
//   PIX due to "catastrophic failures".
//   - Converts to triangle lists.
// - Vulkan requiring 0xFFFF primitive restart index for 16-bit indices and
//   0xFFFFFFFF for 32-bit (Direct3D 12 slightly relaxes this, allowing 0xFFFF
//   for 32-bit also, but it's of no use to Xenia since guest indices are
//   big-endian usually. Also, only 24 lower bits of the vertex index being used
//   on the guest (tested on an Adreno 200 phone with drawing, though not with
//   primitive restart as OpenGL ES 2.0 doesn't expose it), so the upper 8 bits
//   likely shouldn't have effect on primitive restart (guest reset index
//   0xFFFFFF likely working for 0xFFFFFF, 0xFFFFFFFF, and 254 more indices),
//   while Vulkan and Direct3D 12 require exactly 0xFFFFFFFF.
//   - For 16-bit indices with guest reset index other than 0xFFFF (passing
//     0xFFFF directly to the host is fine because it's the same irrespective of
//     endianness), there are two possible solutions:
//     - If the index buffer otherwise doesn't contain 0xFFFF otherwise (since
//       it's a valid vertex index in this case), replacing the primitive reset
//       index with 0xFFFF in the 16-bit buffer.
//     - If the index buffer contains any usage of 0xFFFF as a real vertex
//       index, converting the index buffer to 32-bit, and replacing the
//       primitive reset index with 0xFFFFFFFF.
//   - For 32-bit indices, there are two paths:
//     - If the guest reset index is 0xFFFFFF, and the index buffer actually
//       uses only 0xFFFFFFFF for reset, using it without changes.
//     - If the guest uses something other than 0xFFFFFFFF for primitive reset,
//       replacing elements with (index & 0xFFFFFF) == reset_index with
//       0xFFFFFFFF.
// - Some Vulkan implementations only support 24-bit indices. The guests usually
//   pass big-endian vertices, so we need all 32 bits (as the least significant
//   bits will be in 24...31) to perform the byte swapping. For this reason, we
//   load 32-bit indices indirectly, doing non-indexed draws and fetching the
//   indices from the shared memory. This, however, is not compatible with
//   primitive restart.
//   - Pre-swapping, masking to 24 bits, and converting the reset index to
//     0xFFFFFFFF, resulting in an index buffer that can be used directly.

class PrimitiveProcessor {
 public:
  enum ProcessedIndexBufferType {
    // Auto-indexed on the host.
    kNone,
    // GPU DMA, from the shared memory.
    // For 32-bit, indirection is needed if the host only supports 24-bit
    // indices (even for non-endian-swapped, as the GPU should be ignoring the
    // upper 8 bits completely, rather than exhibiting undefined behavior.
    kGuestDMA,
    // Converted and stored in the primitive converter for the current draw
    // command. For 32-bit indices, if the host doesn't support all 32 bits,
    // this kind of an index buffer will always be pre-masked and pre-swapped.
    kHostConverted,
    // Auto-indexed on the guest, but with an adapter index buffer on the host.
    kHostBuiltinForAuto,
    // Adapter index buffer on the host for indirect loading of indices via DMA
    // (from the shared memory).
    kHostBuiltinForDMA,
  };

  struct ProcessingResult {
    xenos::PrimitiveType guest_primitive_type;
    xenos::PrimitiveType host_primitive_type;
    // Includes whether tessellation is enabled (not kVertex) and the type of
    // tessellation.
    Shader::HostVertexShaderType host_vertex_shader_type;
    // Only used for non-kVertex host_vertex_shader_type. For kAdaptive, the
    // index buffer is always from the guest and fully 32-bit, and contains the
    // floating-point tessellation factors.
    xenos::TessellationMode tessellation_mode;
    // TODO(Triang3l): If important, split into the index count and the actual
    // index buffer size, using zeros for out-of-bounds indices.
    uint32_t host_draw_vertex_count;
    uint32_t line_loop_closing_index;
    ProcessedIndexBufferType index_buffer_type;
    uint32_t guest_index_base;
    xenos::IndexFormat host_index_format;
    xenos::Endian host_shader_index_endian;
    // The reset index, if enabled, is always 0xFFFF for host_index_format
    // kInt16 and 0xFFFFFFFF for kInt32. Never enabled for "list" primitive
    // types, thus safe for direct usage on Vulkan.
    bool host_primitive_reset_enabled;
    // Backend-specific handle for the index buffer valid for the current draw,
    // only valid for index_buffer_type kHostConverted, kHostBuiltinForAuto and
    // kHostBuiltinForDMA.
    size_t host_index_buffer_handle;
    bool IsTessellated() const {
      return Shader::IsHostVertexShaderTypeDomain(host_vertex_shader_type);
    }
  };

  virtual ~PrimitiveProcessor();

  bool AreFull32BitVertexIndicesUsed() const {
    return full_32bit_vertex_indices_used_;
  }
  bool IsConvertingTriangleFansToLists() const {
    return convert_triangle_fans_to_lists_;
  }
  bool IsConvertingLineLoopsToStrips() const {
    return convert_line_loops_to_strips_;
  }
  // Quad lists may be emulated as line lists with adjacency and a geometry
  // shader, but geometry shaders must be supported for this.
  bool IsConvertingQuadListsToTriangleLists() const {
    return convert_quad_lists_to_triangle_lists_;
  }
  bool IsExpandingPointSpritesInVS() const {
    return expand_point_sprites_in_vs_;
  }
  bool IsExpandingRectangleListsInVS() const {
    return expand_rectangle_lists_in_vs_;
  }

  // Submission must be open to call (may request the index buffer in the shared
  // memory).
  bool Process(ProcessingResult& result_out);

  // Invalidates the cache within the range.
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(
      uint32_t physical_address_start, uint32_t length, bool exact_range);

 protected:
  // For host-side index buffer creation, the biggest possibly needed contiguous
  // allocation, in indices.
  // - No conversion: up to 0xFFFF vertices (as the vertex count in
  //   VGT_DRAW_INITIATOR is 16-bit).
  // - Triangle fans to lists: since the 3rd vertex, every guest vertex creates
  //   a triangle, thus the maximum is 3 * (UINT16_MAX - 2), or 0x2FFF7.
  //   Primitive reset can only slow down the amplification - the 3 vertices
  //   after a reset add 1 host vertex each, not 3 each.
  // - Line loops to strips: adding 1 vertex if there are at least 2 vertices in
  //   the original primitive, either replacing the primitive reset index with
  //   this new closing vertex, or in case of the final primitive, just adding a
  //   vertex - thus the absolute limit is UINT16_MAX + 1, or 0x10000.
  // - Quad lists to triangle lists: vertices are processed in groups of 4, each
  //   group converted to 6 vertices, so the limit is 1.5 * 0xFFFC, or 0x17FFA.
  // Thus, the maximum vertex count is defined by triangle fan to list
  // conversion.
  // Also include padding for co-alignment of the source and the destination for
  // SIMD.
  static constexpr uint32_t kMinRequiredConvertedIndexBufferSize =
      sizeof(uint32_t) * (UINT16_MAX - 2) * 3 *
      +XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE;

  PrimitiveProcessor(const RegisterFile& register_file, Memory& memory,
                     TraceWriter& trace_writer, SharedMemory& shared_memory)
      : register_file_(register_file),
        memory_(memory),
        trace_writer_(trace_writer),
        shared_memory_(shared_memory) {}

  // Call from the backend-specific initialization function.
  // - full_32bit_vertex_indices_supported:
  //   - If the backend supports 32-bit indices unconditionally, and doesn't
  //     generate indirection logic in vertex shaders, pass hard-coded `true`.
  //   - Otherwise:
  //     - If the host doesn't support full 32-bit indices (but supports at
  //       least 24-bit indices), pass `false`.
  //     - If the host supports 32-bit indices, but the backend can handle both
  //       cases, pass `cvars::ignore_32bit_vertex_index_support`, and
  //       afterwards, check `AreFull32BitVertexIndicesUsed()` externally to see
  //       if indirection may be needed.
  //     - When full 32-bit indices are not supported, the host must be using
  //       auto-indexed draws for 32-bit indices of ProcessedIndexBufferType
  //       kGuestDMA, while fetching the index data manually from the shared
  //       memory buffer and endian-swapping it.
  //     - Indirection, however, precludes primitive reset usage - so if
  //       primitive reset is needed, the primitive processor will pre-swap and
  //       pre-mask the index buffer so there are only host-endian 0x00###### or
  //       0xFFFFFFFF values in it. In this case, a kHostConverted index buffer
  //       is returned from Process, and indirection is not needed (and
  //       impossible since the index buffer is not in the shared memory buffer
  //       anymore), though byte swap is still needed as 16-bit indices may also
  //       be kHostConverted, while they are completely unaffected by this. The
  //       same applies to primitive type conversion - if it happens for 32-bit
  //       guest indices, and kHostConverted is returned, they will be
  //       pre-swapped and pre-masked.
  // - triangle_fans_supported, line_loops_supported, quad_lists_supported:
  //   - Pass true or false depending on whether the host actually supports
  //     those guest primitive types directly or through geometry shader
  //     emulation. Debug overriding will be resolved in the common code if
  //     needed.
  // - point_sprites_supported_without_vs_expansion,
  //   rectangle_lists_supported_without_vs_expansion:
  //   - Pass true or false depending on whether the host actually supports
  //     those guest primitive types directly or through geometry shader
  //     emulation. Overrides do not apply to these as hosts are not required to
  //     support the fallback paths since they require different vertex shader
  //     structure (for the fallback HostVertexShaderTypes).
  bool InitializeCommon(bool full_32bit_vertex_indices_supported,
                        bool triangle_fans_supported, bool line_loops_supported,
                        bool quad_lists_supported,
                        bool point_sprites_supported_without_vs_expansion,
                        bool rectangle_lists_supported_without_vs_expansion);
  // If any primitive type conversion is needed for auto-indexed draws, called
  // from InitializeCommon (thus only once in the primitive processor's
  // lifetime) to set up the backend's index buffer containing indices for
  // primitive type remapping. The backend must allocate a 4-byte-aligned buffer
  // with `size_bytes` and call fill_callback for its mapping if creation has
  // been successful.
  virtual bool InitializeBuiltinIndexBuffer(
      size_t size_bytes, std::function<void(void*)> fill_callback) = 0;
  // Call last in implementation-specific shutdown, also callable from the
  // destructor.
  void ShutdownCommon();

  // Call at boundaries of lifespans of converted data (between frames,
  // preferably in the end of a frame so between the swap and the next draw,
  // access violation handlers need to do less work).
  void ClearPerFrameCache();

  static constexpr size_t GetBuiltinIndexBufferOffsetBytes(size_t handle) {
    // For simplicity, just using the handles as byte offsets.
    return handle;
  }

  // The destination allocation must have XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE
  // excess bytes.
  static ptrdiff_t GetSimdCoalignmentOffset(const void* host_index_ptr,
                                            uint32_t guest_index_base) {
#if XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE
    // Always moving the host pointer only forward into the allocation padding
    // space of XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE bytes. Without relying on
    // two's complement wrapping overflow behavior, the logic would look like:
    // uintptr_t host_subalignment =
    //     reinterpret_cast<uintptr_t>(host_index_ptr) &
    //     (XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE - 1);
    // uint32_t guest_subalignment = guest_index_base &
    //                               (XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE - 1);
    // uintptr_t host_index_address_aligned = host_index_address;
    // if (guest_subalignment >= host_subalignment) {
    //   return guest_subalignment - host_subalignment;
    // }
    // return XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE -
    //        (host_subalignment - guest_subalignment);
    return ptrdiff_t(
        (guest_index_base - reinterpret_cast<uintptr_t>(host_index_ptr)) &
        (XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE - 1));
#else
    return 0;
#endif
  }

  // Requests a buffer to write the new transformed indices to. The lifetime of
  // the returned buffer must be that of the current frame. Returns the mapping
  // of the buffer to write to, or nullptr in case of failure, in addition to,
  // if successful, a handle that can be used by the backend's command processor
  // to access the backend-specific data for binding the buffer.
  virtual void* RequestHostConvertedIndexBufferForCurrentFrame(
      xenos::IndexFormat format, uint32_t index_count, bool coalign_for_simd,
      uint32_t coalignment_original_address, size_t& backend_handle_out) = 0;

 private:
#if XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE
#if XE_ARCH_AMD64
  // SSSE3 or AVX.
  using SimdVectorU16 = __m128i;
  using SimdVectorU32 = __m128i;
  static SimdVectorU16 ReplicateU16(uint16_t value) {
    return _mm_set1_epi16(int16_t(value));
  }
  static SimdVectorU32 ReplicateU32(uint32_t value) {
    return _mm_set1_epi32(int32_t(value));
  }
  static SimdVectorU16 LoadAlignedVectorU16(const uint16_t* source) {
    return _mm_load_si128(reinterpret_cast<const __m128i*>(source));
  }
  static SimdVectorU32 LoadAlignedVectorU32(const uint32_t* source) {
    return _mm_load_si128(reinterpret_cast<const __m128i*>(source));
  }
  static void StoreUnalignedVectorU16(uint16_t* dest, SimdVectorU16 source) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), source);
  }
  static void StoreUnalignedVectorU32(uint32_t* dest, SimdVectorU32 source) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), source);
  }
#elif XE_ARCH_ARM64
  // NEON.
  using SimdVectorU16 = uint16x8_t;
  using SimdVectorU32 = uint32x4_t;
  static SimdVectorU16 ReplicateU16(uint16_t value) {
    return vdupq_n_u16(value);
  }
  static SimdVectorU32 ReplicateU32(uint32_t value) {
    return vdupq_n_u32(value);
  }
  static SimdVectorU16 LoadAlignedVectorU16(const uint16_t* source) {
#if XE_COMPILER_MSVC
    return vld1q_u16_ex(source, sizeof(uint16x8_t) * CHAR_BIT);
#else
    return vld1q_u16(reinterpret_cast<const uint16_t*>(
        __builtin_assume_aligned(source, sizeof(uint16x8_t))));
#endif
  }
  static SimdVectorU32 LoadAlignedVectorU32(const uint32_t* source) {
#if XE_COMPILER_MSVC
    return vld1q_u32_ex(source, sizeof(uint16x8_t) * CHAR_BIT);
#else
    return vld1q_u32(reinterpret_cast<const uint32_t*>(
        __builtin_assume_aligned(source, sizeof(uint32x4_t))));
#endif
  }
  static void StoreUnalignedVectorU16(uint16_t* dest, SimdVectorU16 source) {
    vst1q_u16(dest, source);
  }
  static void StoreUnalignedVectorU32(uint32_t* dest, SimdVectorU32 source) {
    vst1q_u32(dest, source);
  }
#else
#error SIMD vector types and constant loads not specified.
#endif  // XE_ARCH
  static_assert(
      sizeof(SimdVectorU16) == XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE,
      "XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE must reflect the vector size "
      "actually used");
  static_assert(
      sizeof(SimdVectorU32) == XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE,
      "XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE must reflect the vector size "
      "actually used");
  static constexpr uint32_t kSimdVectorU16Elements =
      sizeof(SimdVectorU16) / sizeof(uint16_t);
  static constexpr uint32_t kSimdVectorU32Elements =
      sizeof(SimdVectorU32) / sizeof(uint32_t);
#endif  // XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE

  static bool IsResetUsed(const uint16_t* source, uint32_t count,
                          uint16_t reset_index_guest_endian);
  static void Get16BitResetIndexUsage(const uint16_t* source, uint32_t count,
                                      uint16_t reset_index_guest_endian,
                                      bool& is_reset_index_used_out,
                                      bool& is_ffff_used_as_vertex_index_out);
  static bool IsResetUsed(const uint32_t* source, uint32_t count,
                          uint32_t reset_index_guest_endian,
                          uint32_t low_bits_mask_guest_endian);
  static void ReplaceResetIndex16To16(uint16_t* dest, const uint16_t* source,
                                      uint32_t count,
                                      uint16_t reset_index_guest_endian);
  // For use when the reset index is not 0xFFFF, and 0xFFFF is also used as a
  // valid index - keeps 0xFFFF as a real index and replaces the reset index
  // with 0xFFFFFFFF instead.
  static void ReplaceResetIndex16To24(uint32_t* dest, const uint16_t* source,
                                      uint32_t count,
                                      uint16_t reset_index_guest_endian);
  // The reset index and the low 24 bits mask are taken explicitly because this
  // function may be used two ways:
  // - Passthrough - when the vertex shader swaps the indices (when 32-bit
  //   indices are supported on the host), in this case HostSwap is kNone, but
  //   the reset index and the guest low bits mask can be swapped according to
  //   the guest endian.
  // - Swapping for the host - when only 24 bits of an index are supported on
  //   the host. In this case, masking and comparison are done before applying
  //   HostSwap, but according to HostSwap, if needed, the data is swapped from
  //   the PowerPC's big endianness to the host GPU little endianness that we
  //   assume, which matches the Xenos's little endianness.
  template <xenos::Endian HostSwap>
  static void ReplaceResetIndex32To24(uint32_t* dest, const uint32_t* source,
                                      uint32_t count,
                                      uint32_t reset_index_guest_endian,
                                      uint32_t low_bits_mask_guest_endian) {
    // The Xbox 360's GPU only uses the low 24 bits of the index - masking.
#if XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE
    while (count && (reinterpret_cast<uintptr_t>(source) &
                     (XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE - 1))) {
      --count;
      uint32_t index = *(source++) & low_bits_mask_guest_endian;
      *(dest++) = index != reset_index_guest_endian
                      ? xenos::GpuSwapInline(index, HostSwap)
                      : UINT32_MAX;
    }
    if (count >= kSimdVectorU32Elements) {
      SimdVectorU32 reset_index_guest_endian_simd =
          ReplicateU32(reset_index_guest_endian);
      SimdVectorU32 low_bits_mask_guest_endian_simd =
          ReplicateU32(low_bits_mask_guest_endian);
#if XE_ARCH_AMD64
      __m128i host_swap_shuffle;
      if constexpr (HostSwap != xenos::Endian::kNone) {
        host_swap_shuffle = _mm_set_epi32(
            int32_t(xenos::GpuSwapInline(uint32_t(0x0F0E0D0C), HostSwap)),
            int32_t(xenos::GpuSwapInline(uint32_t(0x0B0A0908), HostSwap)),
            int32_t(xenos::GpuSwapInline(uint32_t(0x07060504), HostSwap)),
            int32_t(xenos::GpuSwapInline(uint32_t(0x03020100), HostSwap)));
      }
#endif  // XE_ARCH_AMD64
      while (count >= kSimdVectorU32Elements) {
        count -= kSimdVectorU32Elements;
        // Comparison produces 0 or 0xFFFF on AVX and Neon - we need 0xFFFF as
        // the result for the primitive reset indices, so the result is
        // `index | (index == reset_index)`.
        SimdVectorU32 source_simd = LoadAlignedVectorU32(source);
        source += kSimdVectorU32Elements;
        SimdVectorU32 result_simd;
#if XE_ARCH_AMD64
        source_simd =
            _mm_and_si128(source_simd, low_bits_mask_guest_endian_simd);
        result_simd = _mm_or_si128(
            source_simd,
            _mm_cmpeq_epi32(source_simd, reset_index_guest_endian_simd));
        if constexpr (HostSwap != xenos::Endian::kNone) {
          result_simd = _mm_shuffle_epi8(result_simd, host_swap_shuffle);
        }
#elif XE_ARCH_ARM64
        source_simd = vandq_u32(source_simd, low_bits_mask_guest_endian_simd);
        result_simd = vorrq_u32(
            source_simd, vceqq_u32(source_simd, reset_index_guest_endian_simd));
        if constexpr (HostSwap == xenos::Endian::k8in16) {
          result_simd = vreinterpretq_u32_u8(
              vrev16q_u8(vreinterpretq_u8_u32(result_simd)));
        } else if constexpr (HostSwap == xenos::Endian::k8in32) {
          result_simd = vreinterpretq_u32_u8(
              vrev32q_u8(vreinterpretq_u8_u32(result_simd)));
        } else if constexpr (HostSwap == xenos::Endian::k16in32) {
          result_simd = vreinterpretq_u32_u16(
              vrev32q_u16(vreinterpretq_u16_u32(result_simd)));
        }
#else
#error SIMD ReplaceResetIndex32To24 not implemented.
#endif  // XE_ARCH
        StoreUnalignedVectorU32(dest, result_simd);
        dest += kSimdVectorU32Elements;
      }
    }
#endif  // XE_GPU_PRIMITIVE_PROCESSOR_SIMD_SIZE
    while (count--) {
      uint32_t index = *(source++) & low_bits_mask_guest_endian;
      *(dest++) = index != reset_index_guest_endian
                      ? xenos::GpuSwapInline(index, HostSwap)
                      : UINT32_MAX;
    }
  }

  // TODO(Triang3l): 16-bit > 32-bit primitive type conversion for Metal, where
  // primitive reset is always enabled, if UINT16_MAX is used as a real vertex
  // index.

  struct PassthroughIndexTransform {
    uint16_t operator()(uint16_t index) const { return index; }
    uint32_t operator()(uint32_t index) const { return index; }
  };
  struct To24NonSwappingIndexTransform {
    uint32_t operator()(uint32_t index) const {
      return index & xenos::kVertexIndexMask;
    }
  };
  struct To24Swapping8In16IndexTransform {
    uint32_t operator()(uint32_t index) const {
      return xenos::GpuSwapInline(index, xenos::Endian::k8in16) &
             xenos::kVertexIndexMask;
    }
  };
  struct To24Swapping8In32IndexTransform {
    uint32_t operator()(uint32_t index) const {
      return xenos::GpuSwapInline(index, xenos::Endian::k8in32) &
             xenos::kVertexIndexMask;
    }
  };
  struct To24Swapping16In32IndexTransform {
    uint32_t operator()(uint32_t index) const {
      return xenos::GpuSwapInline(index, xenos::Endian::k16in32) &
             xenos::kVertexIndexMask;
    }
  };

  static constexpr uint32_t GetTwoTriangleStripIndexCount(
      uint32_t strip_count) {
    // 4 vertices per strip, and primitive restarts between strips.
    return 4 * strip_count + (std::max(strip_count, UINT32_C(1)) - 1);
  }

  // Triangle fan test cases:
  // - 4D5307E6 - main menu - game logo, developer logo, backgrounds of the menu
  //   item list (the whole menu and individual items) - no index buffer.
  // - 4E4D87E6 - terrain - with an index buffer and primitive reset (note that
  //   there, vfetch indices are computed in the vertex shader, involving
  //   floating-point reciprocal, so this case is very sensitive to rounding,
  //   and incorrect geometry may occur not because of vertex grouping issues,
  //   but also due to the behavior of the vertex shader).
  // Triangle fans as triangle lists.
  // Ordered as (v1, v2, v0), (v2, v3, v0) in Direct3D.
  // https://docs.microsoft.com/en-us/windows/desktop/direct3d9/triangle-fans
  static constexpr uint32_t GetTriangleFanListIndexCount(
      uint32_t fan_index_count) {
    return fan_index_count > 2 ? (fan_index_count - 2) * 3 : 0;
  }
  template <typename Index, typename IndexTransform>
  static void TriangleFanToList(Index* dest, const Index* source,
                                uint32_t source_index_count,
                                const IndexTransform& index_transform) {
    if (source_index_count <= 2) {
      // To match GetTriangleFanListIndexCount.
      return;
    }
    Index index_first = index_transform(source[0]);
    Index index_previous = index_transform(source[1]);
    for (uint32_t i = 2; i < source_index_count; ++i) {
      Index index_current = index_transform(source[i]);
      *(dest++) = index_previous;
      *(dest++) = index_current;
      *(dest++) = index_first;
      index_previous = index_current;
    }
  }

  static constexpr uint32_t GetLineLoopStripIndexCount(
      uint32_t loop_index_count) {
    // Even if 2 vertices are supplied, two lines are still drawn between them.
    // https://www.khronos.org/opengl/wiki/Primitive
    // "You get n lines for n input vertices"
    // "If the user only specifies 1 vertex, the drawing command is ignored"
    return loop_index_count > 1 ? loop_index_count + 1 : 0;
  }
  template <typename Index, typename IndexTransform>
  static void LineLoopToStrip(Index* dest, const Index* source,
                              uint32_t source_index_count,
                              const IndexTransform& index_transform) {
    if (source_index_count <= 1) {
      // To match GetLineLoopStripIndexCount.
      return;
    }
    Index index_first = index_transform(source[0]);
    dest[0] = index_first;
    for (uint32_t i = 1; i < source_index_count; ++i) {
      dest[i] = index_transform(source[i]);
    }
    dest[source_index_count] = index_first;
  }
  static void LineLoopToStrip(uint16_t* dest, const uint16_t* source,
                              uint32_t source_index_count,
                              const PassthroughIndexTransform& index_transform);
  static void LineLoopToStrip(uint32_t* dest, const uint32_t* source,
                              uint32_t source_index_count,
                              const PassthroughIndexTransform& index_transform);

  // Quad list test cases:
  // - 4D5307E6 - main menu - flying dust on the road - no index buffer.
  static constexpr uint32_t GetQuadListTriangleListIndexCount(
      uint32_t quad_list_index_count) {
    return (quad_list_index_count / 4) * 6;
  }
  template <typename Index, typename IndexTransform>
  static void QuadListToTriangleList(Index* dest, const Index* source,
                                     uint32_t source_index_count,
                                     const IndexTransform& index_transform) {
    uint32_t quad_count = source_index_count / 4;
    for (uint32_t i = 0; i < quad_count; ++i) {
      // TODO(Triang3l): Find the correct order.
      // v0, v1, v2.
      Index common_index_0 = index_transform(*(source++));
      *(dest++) = common_index_0;
      *(dest++) = index_transform(*(source++));
      Index common_index_2 = index_transform(*(source++));
      *(dest++) = common_index_2;
      // v0, v2, v3.
      *(dest++) = common_index_0;
      *(dest++) = common_index_2;
      *(dest++) = index_transform(*(source++));
    }
  }

  // Pre-gathering the ranges allows for usage of the same functions for
  // conversion with and without reset. In addition, this increases safety in
  // weird cases - there won't be mismatch between the pre-calculation of the
  // post-conversion index count and the actual conversion if the game for some
  // reason modifies the index buffer between the two and adds or removes reset
  // indices in it.
  struct SinglePrimitiveRange {
    SinglePrimitiveRange(uint32_t guest_offset, uint32_t guest_index_count,
                         uint32_t host_index_count)
        : guest_offset(guest_offset),
          guest_index_count(guest_index_count),
          host_index_count(host_index_count) {}
    uint32_t guest_offset;
    uint32_t guest_index_count;
    uint32_t host_index_count;
  };
  static uint32_t GetMultiPrimitiveHostIndexCountAndRanges(
      std::function<uint32_t(uint32_t)> single_primitive_guest_to_host_count,
      const uint16_t* source, uint32_t source_index_count,
      uint16_t reset_index_guest_endian,
      std::deque<SinglePrimitiveRange>& ranges_append_out);
  static uint32_t GetMultiPrimitiveHostIndexCountAndRanges(
      std::function<uint32_t(uint32_t)> single_primitive_guest_to_host_count,
      const uint32_t* source, uint32_t source_index_count,
      uint32_t reset_index_guest_endian, uint32_t low_bits_mask_guest_endian,
      std::deque<SinglePrimitiveRange>& ranges_append_out);

  template <typename Index, typename IndexTransform,
            typename PrimitiveRangeIterator>
  static void ConvertSinglePrimitiveRanges(
      Index* dest, const Index* source,
      xenos::PrimitiveType source_primitive_type,
      const IndexTransform& index_transform,
      PrimitiveRangeIterator ranges_beginning,
      PrimitiveRangeIterator ranges_end) {
    Index* dest_write_ptr = dest;
    switch (source_primitive_type) {
      case xenos::PrimitiveType::kTriangleFan:
        for (PrimitiveRangeIterator range_it = ranges_beginning;
             range_it != ranges_end; ++range_it) {
          TriangleFanToList(dest_write_ptr, source + range_it->guest_offset,
                            range_it->guest_index_count, index_transform);
          dest_write_ptr += range_it->host_index_count;
        }
        break;
      case xenos::PrimitiveType::kLineLoop:
        for (PrimitiveRangeIterator range_it = ranges_beginning;
             range_it != ranges_end; ++range_it) {
          LineLoopToStrip(dest_write_ptr, source + range_it->guest_offset,
                          range_it->guest_index_count, index_transform);
          dest_write_ptr += range_it->host_index_count;
        }
        break;
      case xenos::PrimitiveType::kQuadList:
        for (PrimitiveRangeIterator range_it = ranges_beginning;
             range_it != ranges_end; ++range_it) {
          QuadListToTriangleList(dest_write_ptr,
                                 source + range_it->guest_offset,
                                 range_it->guest_index_count, index_transform);
          dest_write_ptr += range_it->host_index_count;
        }
        break;
      default:
        assert_unhandled_case(source_primitive_type);
    }
  }

  const RegisterFile& register_file_;
  Memory& memory_;
  TraceWriter& trace_writer_;
  SharedMemory& shared_memory_;

  bool full_32bit_vertex_indices_used_ = false;
  bool convert_triangle_fans_to_lists_ = false;
  bool convert_line_loops_to_strips_ = false;
  bool convert_quad_lists_to_triangle_lists_ = false;
  bool expand_point_sprites_in_vs_ = false;
  bool expand_rectangle_lists_in_vs_ = false;

  // Byte offsets used, for simplicity, directly as handles.
  size_t builtin_ib_offset_two_triangle_strips_ = SIZE_MAX;
  size_t builtin_ib_offset_triangle_fans_to_lists_ = SIZE_MAX;
  size_t builtin_ib_offset_quad_lists_to_triangle_lists_ = SIZE_MAX;

  std::deque<SinglePrimitiveRange> single_primitive_ranges_;

  // Caching for reuse of converted indices within a frame.

  // 256 KB as the largest possible guest index buffer - 0xFFFF 32-bit indices -
  // is slightly smaller than 256 KB, thus cache entries need store links within
  // at most 2 buckets.
  static constexpr uint32_t kCacheBucketSizeBytesLog2 = 18;
  static constexpr uint32_t kCacheBucketSizeBytes =
      uint32_t(1) << kCacheBucketSizeBytesLog2;
  static constexpr uint32_t kCacheBucketCount =
      xe::align(SharedMemory::kBufferSize, kCacheBucketSizeBytes) /
      kCacheBucketSizeBytes;

  union CacheKey {
    uint64_t key;
    struct {
      uint32_t base;                  // 32 total
      uint32_t count : 16;            // 48
      xenos::IndexFormat format : 1;  // 49
      xenos::Endian endian : 2;       // 52
      uint32_t is_reset_enabled : 1;  // 53
      // kNone if not changing the type (like only processing the reset index).
      xenos::PrimitiveType conversion_guest_primitive_type : 6;  // 59
    };

    CacheKey() : key(0) { static_assert_size(*this, sizeof(key)); }
    CacheKey(uint32_t base, uint32_t count, xenos::IndexFormat format,
             xenos::Endian endian, bool is_reset_enabled,
             xenos::PrimitiveType conversion_guest_primitive_type =
                 xenos::PrimitiveType::kNone) {
      // Clear unused bits, then set each field explicitly, not via the
      // initializer list (which causes `uint64_t key = 0;` to be ignored, and
      // also can't contain initializers for aliasing union members).
      key = 0;
      this->base = base;
      this->count = count;
      this->format = format;
      this->endian = endian;
      this->is_reset_enabled = is_reset_enabled;
      this->conversion_guest_primitive_type = conversion_guest_primitive_type;
    }

    struct Hasher {
      size_t operator()(const CacheKey& key) const {
        return std::hash<uint64_t>{}(key.key);
      }
    };
    bool operator==(const CacheKey& other_key) const {
      return key == other_key.key;
    }

    uint32_t GetSizeBytes() const {
      return count * (format == xenos::IndexFormat::kInt16 ? sizeof(uint16_t)
                                                           : sizeof(uint32_t));
    }
  };

  // Subset of ConversionResult that can be reused for different primitive types
  // if the same result is used irrespective of one (like when only processing
  // the reset index).
  struct CachedResult {
    uint32_t host_draw_vertex_count;
    ProcessedIndexBufferType index_buffer_type;
    xenos::IndexFormat host_index_format;
    xenos::Endian host_shader_index_endian;
    bool host_primitive_reset_enabled;
    size_t host_index_buffer_handle;
  };

  struct CacheEntry {
    static_assert(
        UINT16_MAX * sizeof(uint32_t) <=
            (size_t(1) << kCacheBucketSizeBytesLog2),
        "Assuming that primitive processor cache entries need to store to the "
        "previous and to the next entries only within up to 2 buckets, so the "
        "size of the cache buckets must be not smaller than the maximum guest "
        "index buffer size");
    union {
      size_t free_next;
      size_t buckets_prev[2];
    };
    size_t buckets_next[2];
    CacheKey key;
    CachedResult result;
    static uint32_t GetBucketCount(CacheKey key) {
      uint32_t count =
          ((key.base + (key.GetSizeBytes() - 1)) >> kCacheBucketSizeBytesLog2) -
          (key.base >> kCacheBucketSizeBytesLog2) + 1;
      assert_true(count <= 2,
                  "Cache entries only store list links within two buckets");
      return count;
    }
    uint32_t GetBucketCount() const { return GetBucketCount(key); }
  };

  // A cache transaction performs a few operations in a RAII-like way (so
  // processing may return an error for any reason, and won't have to clean up
  // cache_currently_processing_base_ / size_bytes_ explicitly):
  // - Transaction initialization:
  //   - Lookup of previously processed indices in the cache.
  //   - If not found, beginning to add a new entry that is going to be
  //     processed:
  //     - Marking the range as currently being processed, for slightly safer
  //       race condition handling if one happens - if invalidation happens
  //       during the transaction (but outside a global critical region lock,
  //       since processing may take a long time), the new cache entry won't be
  //       stored as it will already be invalid at the time of the completion of
  //       the transaction.
  //     - Enabling an access callback for the range.
  // - Setting the new result after processing (if not found in the cache
  //   previously).
  // - Transaction completion:
  //   - If the range wasn't invalidated during the transaction, storing the new
  //     entry in the cache.
  // If an entry was found in the cache (GetFoundResult results non-null), it
  // MUST be used instead of processing - this class doesn't provide the
  // possibility replace existing entries.
  class CacheTransaction final {
   public:
    CacheTransaction(PrimitiveProcessor& processor, CacheKey key);
    const CachedResult* GetFoundResult() const {
      return result_type_ == ResultType::kExisting ? &result_ : nullptr;
    }
    void SetNewResult(const CachedResult& new_result) {
      // Replacement of an existing entry is not allowed.
      assert_true(result_type_ != ResultType::kExisting);
      result_ = new_result;
      result_type_ = ResultType::kNewSet;
    }
    ~CacheTransaction();

   private:
    PrimitiveProcessor& processor_;
    // If key_.count == 0, this transaction shouldn't do anything - for empty
    // ranges it's pointless, and it's unsafe to get the end pointer without
    // special logic, and count == 0 is also used as a special indicator for
    // vertex count below the cache usage threshold.
    CacheKey key_;
    CachedResult result_;
    enum class ResultType {
      kNewUnset,
      kNewSet,
      kExisting,
    };
    ResultType result_type_ = ResultType::kNewUnset;
  };

  std::deque<CacheEntry> cache_entry_pool_;

  void* memory_invalidation_callback_handle_ = nullptr;

  xe::global_critical_region global_critical_region_;
  // Modified by both the processor and the invalidation callback.
  std::unordered_map<CacheKey, size_t, CacheKey::Hasher> cache_map_;
  // The conversion is performed while the lock is released since it may take a
  // long time.
  // If during the conversion the region currently being converted is
  // invalidated, the current entry will not be added to the cache.
  // Modified by the processor, read by the invalidation callback.
  uint32_t cache_currently_processing_base_ = 0;
  // 0 if not in a cache transaction that hasn't found an existing entry
  // currently.
  uint32_t cache_currently_processing_size_bytes_ = 0;
  // Modified by both the processor and the invalidation callback.
  size_t cache_bucket_free_first_entry_ = SIZE_MAX;
  // Modified by both the processor and the invalidation callback.
  uint64_t cache_buckets_non_empty_l1_[(kCacheBucketCount + 63) / 64] = {};
  // For even faster handling of memory invalidation - whether any bit is set in
  // each cache_buckets_non_empty_l1_.
  // Modified by both the processor and the invalidation callback.
  uint64_t cache_buckets_non_empty_l2_[(kCacheBucketCount + (64 * 64 - 1)) /
                                       (64 * 64)] = {};
  // Must be called in a global critical region.
  void UpdateCacheBucketsNonEmptyL2(
      uint32_t bucket_index_div_64,
      [[maybe_unused]] const global_unique_lock_type&
          global_lock) {
    uint64_t& cache_buckets_non_empty_l2_ref =
        cache_buckets_non_empty_l2_[bucket_index_div_64 >> 6];
    uint64_t cache_buckets_non_empty_l2_bit = uint64_t(1)
                                              << (bucket_index_div_64 & 63);
    if (cache_buckets_non_empty_l1_[bucket_index_div_64]) {
      cache_buckets_non_empty_l2_ref |= cache_buckets_non_empty_l2_bit;
    } else {
      cache_buckets_non_empty_l2_ref &= ~cache_buckets_non_empty_l2_bit;
    }
  }
  // cache_buckets_non_empty_l1_ (along with cache_buckets_non_empty_l2_, which
  // must be kept in sync) used for indication whether each entry is non-empty,
  // for faster clearing (there's no special index here for an empty entry).
  // Huge, so it's the last in the class.
  // Modified by both the processor and the invalidation callback.
  size_t cache_bucket_first_entries_[kCacheBucketCount];
  static std::pair<uint32_t, uint32_t> MemoryInvalidationCallbackThunk(
      void* context_ptr, uint32_t physical_address_start, uint32_t length,
      bool exact_range);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_PRIMITIVE_PROCESSOR_H_
