/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
#define XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/string_buffer.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/metal/dxbc_to_dxil_converter.h"
#include "xenia/gpu/metal/metal_primitive_processor.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/gpu/metal/metal_shader.h"
#include "xenia/gpu/metal/metal_shader_converter.h"
#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/gpu/metal/metal_texture_cache.h"
#include "xenia/ui/metal/metal_api.h"
#include "xenia/ui/metal/metal_provider.h"

namespace xe {
namespace gpu {
namespace metal {

class MetalGraphicsSystem;

class MetalCommandProcessor : public CommandProcessor {
 public:
  explicit MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                 kernel::KernelState* kernel_state);
  ~MetalCommandProcessor();

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;
  void RestoreEdramSnapshot(const void* snapshot) override;

  // Track memory regions written by IssueCopy (resolve) so trace playback
  // can skip overwriting them with stale data from the trace file.
  void MarkResolvedMemory(uint32_t base_ptr, uint32_t length);
  bool IsResolvedMemory(uint32_t base_ptr, uint32_t length) const;
  void ClearResolvedMemory();

  ui::metal::MetalProvider& GetMetalProvider() const;

  // Get the Metal device and command queue
  MTL::Device* GetMetalDevice() const { return device_; }
  MTL::CommandQueue* GetMetalCommandQueue() const { return command_queue_; }

  // Get current render pass descriptor (for render target binding)
  MTL::RenderPassDescriptor* GetCurrentRenderPassDescriptor();

  // Get last captured frame data (for trace dump)
  bool GetLastCapturedFrame(uint32_t& width, uint32_t& height,
                            std::vector<uint8_t>& data);

  // Force issue a swap to push render target to presenter (for trace dumps)
  void ForceIssueSwap();

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  // Flush pending GPU work before entering wait state.
  // This ensures Metal command buffers are submitted and completed before
  // the autorelease pool is drained, preventing hangs from deferred
  // deallocation.
  void PrepareForWait() override;

  // Use base class WriteRegister - don't override with empty implementation!
  // The base class stores values in register_file_->values[] which we need.
  void OnGammaRamp256EntryTableValueWritten() override {}
  void OnGammaRampPWLValueWritten() override {}

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;
  void WriteRegister(uint32_t index, uint32_t value) override;

 private:
  // Initialize shader translation pipeline
  bool InitializeShaderTranslation();

  // Request shared-memory ranges needed for the current draw, mirroring
  // D3D12/Vulkan behavior (vertex buffers, memexport streams).
  bool RequestSharedMemoryRangesForCurrentDraw(MetalShader* vertex_shader,
                                               MetalShader* pixel_shader,
                                               bool memexport_used_vertex,
                                               bool memexport_used_pixel,
                                               bool* any_data_resolved_out);

  // Command buffer management
  void BeginCommandBuffer();
  void EndCommandBuffer();

  // Pipeline state management
  MTL::RenderPipelineState* GetOrCreatePipelineState(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation,
      const RegisterFile& regs);

  // Debug rendering
  bool CreateDebugPipeline();
  void DrawDebugQuad();

  // Constants for descriptor heap sizes.
  // MSC's IR runtime uses a D3D12-like "root signature" model. In D3D12, many
  // root parameters are stage-visible, so VS and PS can both use registers like
  // `t1` / `s0` without colliding because their descriptor tables are bound
  // independently.
  //
  // To mirror that behavior on Metal, keep separate descriptor table slices for
  // VS and PS (per draw), and ring-buffer them so CPU writes don't overwrite
  // data still in flight on the GPU.
  static constexpr size_t kStageCount = 2;  // Vertex + pixel.
  static constexpr size_t kDrawRingCount = 32;

  // Root signature descriptor counts in MetalShaderConverter are intentionally
  // oversized (bindless-style). Allocate extra padding because MSC IR shaders
  // may read one entry past the declared count.
  static constexpr size_t kResourceHeapSlotsPerTable = 1025 + 2;
  static constexpr size_t kSamplerHeapSlotsPerTable = 257 + 2;
  static constexpr size_t kCbvHeapSlotsPerTable = 5 + 2;  // b0-b4 + padding.

  static constexpr size_t kCbvSizeBytes = 4096;
  static constexpr size_t kUniformsBytesPerTable = 5 * kCbvSizeBytes;

  // Top-level argument buffer ring buffer constants
  // Each draw needs its own copy of the top-level pointers to avoid race
  // conditions where later draws overwrite earlier draws' descriptor table
  // pointers before the GPU executes them.
  static constexpr size_t kTopLevelABSlotsPerTable = 32;  // 14 + padding.
  static constexpr size_t kTopLevelABBytesPerTable =
      kTopLevelABSlotsPerTable * sizeof(uint64_t);

  // IR Converter runtime resource binding
  bool CreateIRConverterBuffers();
  void PopulateIRConverterBuffers();

  // System constants population (mirrors D3D12 implementation)
  void UpdateSystemConstantValues(bool shared_memory_is_uav,
                                  bool primitive_polygonal,
                                  uint32_t line_loop_closing_index,
                                  xenos::Endian index_endian,
                                  const draw_util::ViewportInfo& viewport_info,
                                  uint32_t used_texture_mask);

  // Shader modification selection (mirrors D3D12 PipelineCache logic).
  DxbcShaderTranslator::Modification GetCurrentVertexShaderModification(
      const Shader& shader,
      Shader::HostVertexShaderType host_vertex_shader_type,
      uint32_t interpolator_mask) const;
  DxbcShaderTranslator::Modification GetCurrentPixelShaderModification(
      const Shader& shader, uint32_t interpolator_mask, uint32_t param_gen_pos,
      reg::RB_DEPTHCONTROL normalized_depth_control) const;

  // Debug logging utilities
  void DumpUniformsBuffer();
  void LogInterpolators(MetalShader* vertex_shader, MetalShader* pixel_shader);

  // Frame capture
  void CaptureCurrentFrame();

  // Metal device and command queue (from provider)
  MTL::Device* device_ = nullptr;
  MTL::CommandQueue* command_queue_ = nullptr;

  // Render targets
  MTL::Texture* render_target_texture_ = nullptr;
  MTL::Texture* depth_stencil_texture_ = nullptr;
  MTL::RenderPassDescriptor* render_pass_descriptor_ = nullptr;
  uint32_t render_target_width_ = 1280;
  uint32_t render_target_height_ = 720;

  // Current command buffer and encoder
  MTL::CommandBuffer* current_command_buffer_ = nullptr;
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;

  // Shared memory for Xbox 360 memory access
  std::unique_ptr<MetalSharedMemory> shared_memory_;
  std::unique_ptr<MetalPrimitiveProcessor> primitive_processor_;
  bool frame_open_ = false;

 public:
  MetalSharedMemory* shared_memory() const { return shared_memory_.get(); }
  MetalTextureCache* texture_cache() const { return texture_cache_.get(); }

 private:
  // Shader translation components
  std::unique_ptr<DxbcShaderTranslator> shader_translator_;
  std::unique_ptr<DxbcToDxilConverter> dxbc_to_dxil_converter_;
  std::unique_ptr<MetalShaderConverter> metal_shader_converter_;
  StringBuffer ucode_disasm_buffer_;

  // Shader cache (keyed by ucode hash)
  std::unordered_map<uint64_t, std::unique_ptr<MetalShader>> shader_cache_;

  // Pipeline cache (keyed by shader combination)
  std::unordered_map<uint64_t, MTL::RenderPipelineState*> pipeline_cache_;

  // Debug pipeline for testing
  MTL::RenderPipelineState* debug_pipeline_ = nullptr;

  // Frame capture data
  bool has_captured_frame_ = false;
  uint32_t captured_width_ = 0;
  uint32_t captured_height_ = 0;
  std::vector<uint8_t> captured_frame_data_;

  // Texture cache for guest texture uploads
  std::unique_ptr<MetalTextureCache> texture_cache_;

  // Render target cache for framebuffer management
  std::unique_ptr<MetalRenderTargetCache> render_target_cache_;

  // IR Converter runtime buffers for shader resource binding
  MTL::Buffer* null_buffer_ = nullptr;  // Null buffer for unused descriptors
  MTL::Texture* null_texture_ =
      nullptr;  // Placeholder texture for unbound slots
  MTL::SamplerState* null_sampler_ =
      nullptr;                          // Default sampler for unbound slots
  MTL::Buffer* res_heap_ab_ = nullptr;  // Resource descriptor heap (SRVs/UAVs)
  MTL::Buffer* smp_heap_ab_ = nullptr;  // Sampler descriptor heap
  MTL::Buffer* cbv_heap_ab_ = nullptr;  // CBV descriptor heap (b0-b3)
  MTL::Buffer* uniforms_buffer_ = nullptr;  // Raw constant buffer data
  MTL::Buffer* top_level_ab_ =
      nullptr;  // Top-level argument buffer (bind point 2)
  MTL::Buffer* draw_args_buffer_ =
      nullptr;  // Draw arguments buffer (bind point 4)

  // System constants - matches DxbcShaderTranslator::SystemConstants layout
  // Stored persistently to track dirty state
  DxbcShaderTranslator::SystemConstants system_constants_;
  bool system_constants_dirty_ = true;
  bool logged_missing_texture_warning_ = false;

  // Draw counter for ring-buffer descriptor heap allocation
  // Each draw uses a different region of the descriptor heap to avoid
  // overwriting previous draws' descriptors before GPU execution
  uint32_t current_draw_index_ = 0;

  // Track memory regions written by IssueCopy (resolve) during trace playback.

  // This prevents the trace player from overwriting resolved data with stale
  // data from the trace file.
  struct ResolvedRange {
    uint32_t base;
    uint32_t length;
  };
  std::vector<ResolvedRange> resolved_memory_ranges_;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
