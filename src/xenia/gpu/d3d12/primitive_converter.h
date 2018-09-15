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

#include "xenia/gpu/d3d12/shared_memory.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_context.h"

namespace xe {
namespace gpu {
namespace d3d12 {

// Index buffer cache for primitive types not natively supported by Direct3D 12:
// - Triangle and line strips with non-0xFFFF/0xFFFFFFFF reset index.
// - Triangle fans.
// - Line loops (only indexed ones - non-indexed are better handled in vertex
//   shaders, otherwise a whole index buffer would have to be created for every
//   vertex count value).
class PrimitiveConverter {
 public:
  PrimitiveConverter(ui::d3d12::D3D12Context* context,
                     RegisterFile* register_file, Memory* memory,
                     SharedMemory* shared_memory);
  ~PrimitiveConverter();

  void Initialize();
  void Shutdown();
  void ClearCache();

  void BeginFrame(ID3D12GraphicsCommandList* command_list);
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

  // TODO(Triang3l): A function that returns a static index buffer for
  // non-indexed drawing of unsupported primitives

 private:
  // simd_offset is source address & 15 - if SIMD is used, the source and the
  // target must have the same alignment within one register. 0 is optimal when
  // not using SIMD.
  void* AllocateIndices(IndexFormat format, uint32_t count,
                        uint32_t simd_offset,
                        D3D12_GPU_VIRTUAL_ADDRESS& gpu_address_out);

  ui::d3d12::D3D12Context* context_;
  RegisterFile* register_file_;
  Memory* memory_;
  SharedMemory* shared_memory_;

  std::unique_ptr<ui::d3d12::UploadBufferPool> buffer_pool_ = nullptr;

  struct ConvertedIndices {
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;
    PrimitiveType primitive_type;
    uint32_t index_count;
    IndexFormat index_format;
    // Index pre-swapped - in guest storage endian.
    uint32_t reset_index;
    bool reset;
  };
  // Cache for a single frame.
  std::unordered_multimap<uint32_t, ConvertedIndices> converted_indices_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PRIMITIVE_CONVERTER_H_
