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

#include <string>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

#ifdef METAL_SHADER_CONVERTER_AVAILABLE
#include <metal_irconverter.h>
#include <metal_irconverter_runtime.h>
#endif  // METAL_SHADER_CONVERTER_AVAILABLE
#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalGraphicsSystem;
class MetalBufferCache;
class MetalPipelineCache;
class MetalPrimitiveProcessor;
class MetalRenderTargetCache;
class MetalSharedMemory;
class MetalTextureCache;

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

  // Command processing
  bool BeginSubmission(bool is_guest_command);
  bool EndSubmission(bool is_swap);
  bool CanEndSubmissionImmediately() const;

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
  
  // Frame tracking
  uint32_t frame_count_ = 0;
  
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_COMMAND_PROCESSOR_H_
