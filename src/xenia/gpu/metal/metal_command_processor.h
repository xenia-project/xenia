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

  // Command buffer management
  void BeginCommandBuffer();
  void EndCommandBuffer();

  // Pipeline state management
  MTL::RenderPipelineState* GetOrCreatePipelineState(
      MetalShader::MetalTranslation* vertex_translation,
      MetalShader::MetalTranslation* pixel_translation);

  // Debug rendering
  bool CreateDebugPipeline();
  void DrawDebugQuad();

  // Constants for descriptor heap sizes.
  static constexpr size_t kResourceHeapSlotCount = 1026;
  static constexpr size_t kSamplerHeapSlotCount = 258;
  static constexpr size_t kCbvHeapSlotCount = 6;

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

 public:
  MetalSharedMemory* shared_memory() const { return shared_memory_.get(); }

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
