/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_PRIMITIVE_CONVERTER_H_
#define XENIA_GPU_D3D12_PRIMITIVE_CONVERTER_H_

#include <atomic>
#include <memory>
#include <unordered_map>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_context.h"
#include "xenia/ui/d3d12/pools.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class D3D12CommandProcessor;

// Index buffer cache for primitive types not natively supported by Direct3D 12:
// - Triangle and line strips with non-0xFFFF/0xFFFFFFFF reset index.
// - Triangle fans.
// - Line loops (only indexed ones - non-indexed are better handled in vertex
//   shaders, otherwise a whole index buffer would have to be created for every
//   vertex count value).
// - Quad lists (for debugging since geometry shaders break PIX - as an
//   alternative to the geometry shader).
class PrimitiveConverter {
 public:
  PrimitiveConverter(D3D12CommandProcessor* command_processor,
                     RegisterFile* register_file, Memory* memory);
  ~PrimitiveConverter();

  bool Initialize();
  void Shutdown();
  void ClearCache();

  void BeginFrame();
  void EndFrame();

  // Returns the primitive type that the original type will be converted to.
  static PrimitiveType GetReplacementPrimitiveType(PrimitiveType type);

  enum class ConversionResult {
    // Converted to a transient buffer.
    kConverted,
    // Conversion not required - use the index buffer in shared memory.
    kConversionNotNeeded,
    // No errors, but nothing to render.
    kPrimitiveEmpty,
    // Total failure of the draw call.
    kFailed
  };

  // Converts an index buffer to the primitive type returned by
  // GetReplacementPrimitiveType. If conversion has been performed, the returned
  // buffer will be in the GENERIC_READ state (it's in an upload heap). Only
  // writing to the outputs if returning kConverted. The restart index will be
  // handled internally from the register values.
  ConversionResult ConvertPrimitives(PrimitiveType source_type,
                                     uint32_t address, uint32_t index_count,
                                     IndexFormat index_format,
                                     Endian index_endianness,
                                     D3D12_GPU_VIRTUAL_ADDRESS& gpu_address_out,
                                     uint32_t& index_count_out);

  // Returns the 16-bit index buffer for drawing unsupported non-indexed
  // primitives in INDEX_BUFFER state, for non-indexed drawing. Returns 0 if
  // conversion is not available (can draw natively).
  D3D12_GPU_VIRTUAL_ADDRESS GetStaticIndexBuffer(
      PrimitiveType source_type, uint32_t index_count,
      uint32_t& index_count_out) const;

 private:
  // simd_offset is source address & 15 - if SIMD is used, the source and the
  // target must have the same alignment within one register. 0 is optimal when
  // not using SIMD.
  void* AllocateIndices(IndexFormat format, uint32_t count,
                        uint32_t simd_offset,
                        D3D12_GPU_VIRTUAL_ADDRESS& gpu_address_out);

  // Callback for invalidating buffers mid-frame.
  void MemoryWriteCallback(uint32_t page_first, uint32_t page_last);
  static void MemoryWriteCallbackThunk(void* context_ptr, uint32_t page_first,
                                       uint32_t page_last);

  D3D12CommandProcessor* command_processor_;
  RegisterFile* register_file_;
  Memory* memory_;

  std::unique_ptr<ui::d3d12::UploadBufferPool> buffer_pool_ = nullptr;

  // Static index buffers for emulating unsupported primitive types when drawing
  // without an index buffer.
  // CPU-side, used only for uploading - destroyed once the copy commands have
  // been completed.
  ID3D12Resource* static_ib_upload_ = nullptr;
  uint64_t static_ib_upload_frame_;
  // GPU-side - used for drawing.
  ID3D12Resource* static_ib_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS static_ib_gpu_address_;
  // In PM4 draw packets, 16 bits are used for the vertex count.
  static constexpr uint32_t kMaxNonIndexedVertices = 65535;
  static constexpr uint32_t kStaticIBTriangleFanOffset = 0;
  static constexpr uint32_t kStaticIBTriangleFanCount =
      (kMaxNonIndexedVertices - 2) * 3;
  static constexpr uint32_t kStaticIBQuadOffset =
      kStaticIBTriangleFanOffset + kStaticIBTriangleFanCount;
  static constexpr uint32_t kStaticIBQuadCount =
      (kMaxNonIndexedVertices >> 2) * 6;
  static constexpr uint32_t kStaticIBTotalCount =
      kStaticIBQuadOffset + kStaticIBQuadCount;

  // Not identifying the index buffer uniquely - reset index must also be
  // checked if reset is enabled.
  union ConvertedIndicesKey {
    uint64_t value;
    struct {
      uint32_t address;               // 32
      PrimitiveType source_type : 6;  // 38
      IndexFormat format : 1;         // 39
      uint32_t count : 16;            // 55
      uint32_t reset : 1;             // 56
    };

    // Clearing the unused bits.
    ConvertedIndicesKey() : value(0) {}
    ConvertedIndicesKey(const ConvertedIndicesKey& key) : value(key.value) {}
    ConvertedIndicesKey& operator=(const ConvertedIndicesKey& key) {
      value = key.value;
      return *this;
    }
    bool operator==(const ConvertedIndicesKey& key) const {
      return value == key.value;
    }
    bool operator!=(const ConvertedIndicesKey& key) const {
      return value != key.value;
    }
  };

  struct ConvertedIndices {
    ConvertedIndicesKey key;
    // If reset is enabled, this also must be checked to find cached indices.
    uint32_t reset_index;

    // Zero GPU address if conversion not needed or the resulting index buffer
    // is empty.
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    // When conversion is not needed, this must be equal to the original index
    // count.
    uint32_t converted_index_count;
  };

  // Cache for a single frame.
  std::unordered_multimap<uint64_t, ConvertedIndices> converted_indices_cache_;

  // Very coarse cache invalidation - if something is modified in a 8 MB portion
  // of the physical memory and converted indices are also there, invalidate all
  // the cache.
  uint64_t memory_regions_used_;
  std::atomic<uint64_t> memory_regions_invalidated_ = 0;
  void* physical_write_watch_handle_ = nullptr;
  uint32_t system_page_size_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PRIMITIVE_CONVERTER_H_
