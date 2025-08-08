// Metal Debug Utilities for Xenia GPU
// Copyright 2024 Ben Vanik. All rights reserved.
// Released under the BSD license - see LICENSE in the root for more details.

#ifndef XENIA_GPU_METAL_DEBUG_UTILS_H_
#define XENIA_GPU_METAL_DEBUG_UTILS_H_

#include <string>
#include <vector>

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "xenia/base/logging.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/registers.h"

// Include Metal IR Converter Runtime for IRDescriptorTableEntry
#define IR_RUNTIME_METALCPP
#include "/usr/local/include/metal_irconverter_runtime/metal_irconverter_runtime.h"

namespace xe {
namespace gpu {
namespace metal {

// Forward declarations
class MetalCommandProcessor;

// Metal debugging utility class
class MetalDebugUtils {
 public:
  MetalDebugUtils(MetalCommandProcessor* command_processor);
  ~MetalDebugUtils();
  
  // Initialize debug features
  void Initialize(MTL::CommandQueue* command_queue);
  
  // GPU Capture management
  void BeginProgrammaticCapture();
  void EndProgrammaticCapture();
  bool ShouldCaptureFrame(uint32_t frame_index);
  void SetCaptureMode(const std::string& mode); // "frame", "command", "draw"
  
  // State validation
  void ValidateDrawState(uint32_t draw_index, RegisterFile* register_file);
  void ValidateDescriptorHeap(MTL::Buffer* heap, const std::string& name);
  void ValidateTexture(MTL::Texture* texture, uint32_t slot, const std::string& name);
  
  // Resource labeling
  void LabelTexture(MTL::Texture* texture, uint32_t width, uint32_t height, 
                    uint32_t format, uint32_t heap_slot);
  void LabelPipeline(MTL::RenderPipelineState* pipeline, 
                     uint64_t vertex_shader_hash, uint64_t pixel_shader_hash);
  void LabelCommandEncoder(MTL::RenderCommandEncoder* encoder, 
                           uint32_t draw_index, uint32_t batch_index);
  void LabelBuffer(MTL::Buffer* buffer, const std::string& name);
  
  // Debug groups for GPU capture
  void PushDebugGroup(MTL::RenderCommandEncoder* encoder, const std::string& name);
  void PopDebugGroup(MTL::RenderCommandEncoder* encoder);
  
  // Texture dumping
  void DumpTexture(MTL::Texture* texture, const std::string& name);
  void DumpAllBoundTextures();
  void DumpDescriptorHeap(MTL::Buffer* heap, const std::string& name);
  
  // State dumping
  void DumpPipelineState(MTL::RenderPipelineState* pipeline);
  void DumpViewportScissor(RegisterFile* register_file);
  void DumpRenderTargets();
  
  // Feature flags
  void SetValidationEnabled(bool enabled) { validation_enabled_ = enabled; }
  void SetCaptureModeEnabled(bool enabled) { capture_enabled_ = enabled; }
  void SetDumpTexturesEnabled(bool enabled) { dump_textures_ = enabled; }
  void SetDumpStateEnabled(bool enabled) { dump_state_ = enabled; }
  void SetLabelResourcesEnabled(bool enabled) { label_resources_ = enabled; }
  
  // Helpers
  static std::string FormatTextureLabel(uint32_t width, uint32_t height, 
                                        uint32_t format, uint32_t slot);
  static std::string FormatPipelineLabel(uint64_t vs_hash, uint64_t ps_hash);
  
 private:
  MetalCommandProcessor* command_processor_;
  MTL::CommandQueue* command_queue_ = nullptr;
  MTL::CaptureManager* capture_manager_ = nullptr;
  
  // Capture state
  bool capture_enabled_ = false;
  std::string capture_mode_ = "command";  // "frame", "command", "draw"
  uint32_t capture_frame_index_ = 0;
  uint32_t captured_count_ = 0;
  bool is_capturing_ = false;
  
  // Debug flags
  bool validation_enabled_ = false;
  bool dump_textures_ = false;
  bool dump_state_ = false;
  bool label_resources_ = true;
  
  // Statistics
  uint32_t zero_scissor_count_ = 0;
  uint32_t invalid_viewport_count_ = 0;
  uint32_t empty_descriptor_count_ = 0;
  uint32_t null_texture_count_ = 0;
  
  // Helper methods
  void CreateCaptureScope(const std::string& name);
  bool CheckTextureIsEmpty(MTL::Texture* texture);
  void LogStatistics();
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_DEBUG_UTILS_H_