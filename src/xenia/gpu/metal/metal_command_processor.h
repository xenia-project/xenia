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

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

#ifdef METAL_SHADER_CONVERTER_AVAILABLE
#include "third_party/metal-shader-converter/include/metal_irconverter.h"
// Forward declarations only - actual definitions come from IR Runtime implementation
struct IRDescriptorTableEntry;
#endif  // METAL_SHADER_CONVERTER_AVAILABLE

namespace xe {
namespace gpu {
namespace metal {

// Forward declaration
class MetalDebugUtils;

class MetalGraphicsSystem;
class MetalBufferCache;
class MetalPipelineCache;
class MetalPrimitiveProcessor;
class MetalRenderTargetCache;
class MetalSharedMemory;
class MetalTextureCache;

// IRDescriptorTableEntry is defined in metal_irconverter_runtime.h

class MetalCommandProcessor : public CommandProcessor {
 public:
  MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                        kernel::KernelState* kernel_state);
  ~MetalCommandProcessor() override;

  std::string GetWindowTitleText() const;

  // Metal device access
  MTL::Device* GetMetalDevice() const;
  MTL::CommandQueue* GetMetalCommandQueue() const;
  
  // Cache system access
  MetalBufferCache* buffer_cache() const { return buffer_cache_.get(); }
  MetalPipelineCache* pipeline_cache() const { return pipeline_cache_.get(); }
  MetalPrimitiveProcessor* primitive_processor() const { return primitive_processor_.get(); }
  MetalRenderTargetCache* render_target_cache() const { return render_target_cache_.get(); }
  MetalTextureCache* texture_cache() const { return texture_cache_.get(); }
  
  // Direct framebuffer capture (for trace dumps without presenter)
  bool CaptureColorTarget(uint32_t index, uint32_t& width, uint32_t& height,
                         std::vector<uint8_t>& data);
                         
  // Get last captured frame (for trace dumps)
  bool GetLastCapturedFrame(uint32_t& width, uint32_t& height, std::vector<uint8_t>& data);
  
  // Helper for depth buffer visualization
  void ConvertDepthToRGBA(std::vector<uint8_t>& data, uint32_t width, uint32_t height, MTL::PixelFormat format);

  // Command processing
  bool BeginSubmission(bool is_guest_command);
  bool EndSubmission(bool is_swap);
  bool CanEndSubmissionImmediately() const;
  
  // Commit any pending command buffer (used by trace dump)
  void CommitPendingCommandBuffer();

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  void OnGammaRamp256EntryTableValueWritten() override;
  void OnGammaRampPWLValueWritten() override;

  // Pure virtual methods from CommandProcessor that must be implemented
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

  void RestoreEdramSnapshot(const void* snapshot) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;

  bool IssueCopy() override;

  // Called when the primary buffer ends - we submit here to reduce latency
  void OnPrimaryBufferEnd() override;
  
  // WaitForIdle to flush any pending submissions
  void WaitForIdle();

 private:
  MetalGraphicsSystem* metal_graphics_system_;

  // Command processing state
  bool submission_open_ = false;
  bool frame_open_ = false;
  MTL::CommandBuffer* current_command_buffer_ = nullptr;

  // Cache systems
  std::unique_ptr<MetalBufferCache> buffer_cache_;
  std::unique_ptr<MetalPipelineCache> pipeline_cache_;
  std::unique_ptr<MetalPrimitiveProcessor> primitive_processor_;
  std::unique_ptr<MetalRenderTargetCache> render_target_cache_;
  std::unique_ptr<MetalSharedMemory> shared_memory_;
  std::unique_ptr<MetalTextureCache> texture_cache_;
  
  // Dummy render target for traces with no color buffers
  MTL::Texture* dummy_color_target_ = nullptr;
  
  // Frame tracking
  uint32_t frame_count_ = 0;
  
  // Draw batching to avoid too many in-flight command buffers
  int draws_in_current_batch_ = 0;
  
  // Active command buffer for draw commands (used in IssueDraw)
  MTL::CommandBuffer* active_draw_command_buffer_ = nullptr;
  
  // Current render encoder - reused across multiple draws
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;
  MTL::RenderPassDescriptor* current_render_pass_ = nullptr;
  
  // Last captured frame for trace dumps
  std::vector<uint8_t> last_captured_frame_data_;
  uint32_t last_captured_frame_width_ = 0;
  uint32_t last_captured_frame_height_ = 0;
  
  // Last render pass descriptor used in IssueDraw for capture fallback
  MTL::RenderPassDescriptor* last_render_pass_descriptor_ = nullptr;
  
  // Debug red shader pipeline for testing
  MTL::RenderPipelineState* debug_red_pipeline_ = nullptr;
  
  // Shader constant staging buffers
  // Updated by WriteRegister during trace replay (LOAD_ALU_CONSTANT)
  float* cb0_staging_ = nullptr;  // VS constants (256 vec4s)
  float* cb1_staging_ = nullptr;  // PS constants (256 vec4s)
  
  // Descriptor heap argument buffers for Metal Shader Converter
  MTL::ArgumentEncoder* res_heap_encoder_ = nullptr;   // [[buffer(3)]] - textures
  MTL::Buffer*          res_heap_ab_ = nullptr;
  MTL::ArgumentEncoder* smp_heap_encoder_ = nullptr;   // [[buffer(4)]] - samplers
  MTL::Buffer*          smp_heap_ab_ = nullptr;
  MTL::Buffer*          uav_heap_ab_ = nullptr;        // UAV descriptor heap
  MTL::Buffer*          uniforms_buffer_ = nullptr;    // Cached uniforms buffer for CBVs
  
  // Helper to create debug pipeline
  void CreateDebugRedPipeline();
  
  // Debug utilities (optional)
  std::unique_ptr<MetalDebugUtils> debug_utils_;
  
  // Render state tracking to avoid redundant state setting
  struct RenderStateCache {
    MTL::CullMode current_cull_mode = MTL::CullModeNone;
    MTL::Winding current_winding = MTL::WindingClockwise;
    bool current_depth_write_enabled = true;
    MTL::CompareFunction current_depth_compare = MTL::CompareFunctionAlways;
    // Add more state as needed
  } render_state_cache_;
  
  // REMOVED: Null texture code was causing autorelease pool crashes
  // When textures are missing, we'll skip binding them or duplicate texture 0
  
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
