// Metal Debug Utilities for Xenia GPU
// Copyright 2024 Ben Vanik. All rights reserved.
// Released under the BSD license - see LICENSE in the root for more details.

#include "xenia/gpu/metal/metal_debug_utils.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/base/string.h"
#include "third_party/metal-cpp/Foundation/Foundation.hpp"
#include "third_party/fmt/include/fmt/format.h"
#include <cstdio>
#include <cstdlib>

namespace xe {
namespace gpu {
namespace metal {

// IRDescriptorTableEntry is defined in metal_irconverter_runtime.h
// No need to redefine it here

MetalDebugUtils::MetalDebugUtils(MetalCommandProcessor* command_processor) 
    : command_processor_(command_processor) {
  XELOGI("MetalDebugUtils initialized");
}

MetalDebugUtils::~MetalDebugUtils() {
  LogStatistics();
}

void MetalDebugUtils::Initialize(MTL::CommandQueue* command_queue) {
  command_queue_ = command_queue;
  capture_manager_ = MTL::CaptureManager::sharedCaptureManager();
  
  // Check if capture is supported
  if (capture_manager_) {
    XELOGI("Metal GPU capture manager initialized");
  } else {
    XELOGW("Metal GPU capture manager not available");
  }
}

void MetalDebugUtils::BeginProgrammaticCapture() {
  if (!capture_enabled_ || !capture_manager_ || is_capturing_) {
    return;
  }
  
  // Create capture descriptor
  auto* descriptor = MTL::CaptureDescriptor::alloc()->init();
  descriptor->setCaptureObject(command_queue_);
  
  // Set destination to file
  descriptor->setDestination(MTL::CaptureDestinationGPUTraceDocument);
  
  // Create output URL (check for debug directory from environment)
  const char* capture_dir = std::getenv("XENIA_GPU_CAPTURE_DIR");
  std::string filename = capture_dir 
    ? fmt::format("{}/gpu_capture_{:04d}.gputrace", capture_dir, captured_count_++)
    : fmt::format("/tmp/xenia_gpu_capture_{:04d}.gputrace", captured_count_++);
  
  auto* url = NS::URL::fileURLWithPath(
      NS::String::string(filename.c_str(), NS::UTF8StringEncoding));
  descriptor->setOutputURL(url);
  
  // Start capture
  NS::Error* error = nullptr;
  if (capture_manager_->startCapture(descriptor, &error)) {
    XELOGI("Started GPU capture to {}", filename);
    is_capturing_ = true;
  } else {
    XELOGE("Failed to start GPU capture: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown");
  }
  
  descriptor->release();
}

void MetalDebugUtils::EndProgrammaticCapture() {
  if (capture_manager_ && is_capturing_) {
    capture_manager_->stopCapture();
    XELOGI("GPU capture completed (frame {})", capture_frame_index_);
    is_capturing_ = false;
  }
}

bool MetalDebugUtils::ShouldCaptureFrame(uint32_t frame_index) {
  // Capture specific frames for debugging
  // Can be configured via command line or config file
  return capture_enabled_ && (frame_index == capture_frame_index_);
}

void MetalDebugUtils::SetCaptureMode(const std::string& mode) {
  capture_mode_ = mode;
  XELOGI("Metal capture mode set to: {}", mode);
}

void MetalDebugUtils::ValidateDrawState(uint32_t draw_index, RegisterFile* register_file) {
  if (!validation_enabled_) return;
  
  // Check scissor
  uint32_t scissor_tl = register_file->values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL];
  uint32_t scissor_x = scissor_tl & 0x7FFF;
  uint32_t scissor_y = (scissor_tl >> 16) & 0x7FFF;
  
  uint32_t scissor_br = register_file->values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];
  uint32_t scissor_width = (scissor_br & 0x7FFF) - scissor_x;
  uint32_t scissor_height = ((scissor_br >> 16) & 0x7FFF) - scissor_y;
  
  if (scissor_width == 0 || scissor_height == 0) {
    XELOGW("Draw {} has zero scissor: {}x{} at ({},{})",
           draw_index, scissor_width, scissor_height, scissor_x, scissor_y);
    zero_scissor_count_++;
  }
  
  // Check viewport
  float viewport_scale_x = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_XSCALE]);
  float viewport_scale_y = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_YSCALE]);
  float vp_width = std::abs(viewport_scale_x * 2.0f);
  float vp_height = std::abs(viewport_scale_y * 2.0f);
  
  if (vp_width <= 0 || vp_height <= 0) {
    XELOGW("Draw {} has invalid viewport: {}x{}", draw_index, vp_width, vp_height);
    invalid_viewport_count_++;
  }
  
  // Log state if dumping is enabled
  if (dump_state_) {
    XELOGI("Draw {} state: scissor={}x{}+{}+{}, viewport={}x{}", 
           draw_index, scissor_width, scissor_height, scissor_x, scissor_y,
           vp_width, vp_height);
  }
}

void MetalDebugUtils::ValidateDescriptorHeap(MTL::Buffer* heap, const std::string& name) {
  if (!validation_enabled_ || !heap) return;
  
  XELOGI("ValidateDescriptorHeap: Checking {} heap at buffer {:p}", name, (void*)heap);
  
  auto* heap_data = (IRDescriptorTableEntry*)heap->contents();
  bool has_valid_entry = false;
  uint32_t valid_count = 0;
  
  for (size_t i = 0; i < 32; ++i) {
    if (heap_data[i].gpuVA != 0 || 
        heap_data[i].textureViewID != 0 || 
        heap_data[i].metadata != 0) {
      has_valid_entry = true;
      valid_count++;
      
      if (dump_state_) {
        XELOGI("{} heap[{}]: gpuVA=0x{:x}, tex=0x{:x}, meta=0x{:x}", 
               name, i, 
               heap_data[i].gpuVA,
               heap_data[i].textureViewID,
               heap_data[i].metadata);
      }
    }
  }
  
  if (!has_valid_entry) {
    XELOGW("{} descriptor heap is empty!", name);
    empty_descriptor_count_++;
  } else {
    XELOGI("{} descriptor heap has {} valid entries", name, valid_count);
  }
}

void MetalDebugUtils::ValidateTexture(MTL::Texture* texture, uint32_t slot, 
                                      const std::string& name) {
  if (!validation_enabled_) return;
  
  if (!texture) {
    XELOGW("Null texture at slot {} ({})", slot, name);
    null_texture_count_++;
    return;
  }
  
  if (CheckTextureIsEmpty(texture)) {
    XELOGW("Texture at slot {} ({}) appears to be empty", slot, name);
  }
}

void MetalDebugUtils::LabelTexture(MTL::Texture* texture, uint32_t width, 
                                   uint32_t height, uint32_t format, uint32_t heap_slot) {
  if (!label_resources_ || !texture) return;
  
  std::string label = FormatTextureLabel(width, height, format, heap_slot);
  texture->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));
}

void MetalDebugUtils::LabelPipeline(MTL::RenderPipelineState* pipeline,
                                    uint64_t vertex_shader_hash, 
                                    uint64_t pixel_shader_hash) {
  if (!label_resources_ || !pipeline) return;
  
  std::string label = FormatPipelineLabel(vertex_shader_hash, pixel_shader_hash);
  // Note: RenderPipelineState doesn't have setLabel in Metal-cpp
  // This would need to be done at creation time via descriptor
}

void MetalDebugUtils::LabelCommandEncoder(MTL::RenderCommandEncoder* encoder,
                                          uint32_t draw_index, uint32_t batch_index) {
  if (!label_resources_ || !encoder) return;
  
  std::string label = fmt::format("Draw_{}_Batch_{}", draw_index, batch_index);
  encoder->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));
}

void MetalDebugUtils::LabelBuffer(MTL::Buffer* buffer, const std::string& name) {
  if (!label_resources_ || !buffer) return;
  
  buffer->setLabel(NS::String::string(name.c_str(), NS::UTF8StringEncoding));
}

void MetalDebugUtils::PushDebugGroup(MTL::RenderCommandEncoder* encoder, 
                                     const std::string& name) {
  if (!encoder) return;
  
  encoder->pushDebugGroup(NS::String::string(name.c_str(), NS::UTF8StringEncoding));
}

void MetalDebugUtils::PopDebugGroup(MTL::RenderCommandEncoder* encoder) {
  if (!encoder) return;
  
  encoder->popDebugGroup();
}

void MetalDebugUtils::DumpTexture(MTL::Texture* texture, const std::string& name) {
  if (!dump_textures_ || !texture) return;
  
  size_t width = texture->width();
  size_t height = texture->height();
  size_t bytes_per_row = width * 4;
  
  std::vector<uint8_t> data(bytes_per_row * height);
  
  // Read texture data
  texture->getBytes(data.data(), bytes_per_row,
                    MTL::Region::Make2D(0, 0, width, height), 0);
  
  // Generate filename (check for debug directory from environment)
  const char* texture_dir = std::getenv("XENIA_TEXTURE_DUMP_DIR");
  std::string filename = texture_dir
    ? fmt::format("{}/xenia_tex_{}_{:04d}_{}x{}.raw", texture_dir, name, captured_count_, width, height)
    : fmt::format("/tmp/xenia_tex_{}_{:04d}_{}x{}.raw", name, captured_count_, width, height);
  
  // Write raw data
  FILE* file = std::fopen(filename.c_str(), "wb");
  if (file) {
    std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);
    XELOGI("Dumped texture {} ({}x{}) to {}", name, width, height, filename);
    
    // Check if empty
    if (CheckTextureIsEmpty(texture)) {
      XELOGW("Dumped texture {} is empty!", name);
    }
  } else {
    XELOGE("Failed to create file {}", filename);
  }
}

void MetalDebugUtils::DumpAllBoundTextures() {
  // This would need access to the command processor's bound textures
  // Implementation depends on how textures are tracked
  XELOGI("DumpAllBoundTextures - implementation pending");
}

void MetalDebugUtils::DumpDescriptorHeap(MTL::Buffer* heap, const std::string& name) {
  if (!dump_state_ || !heap) return;
  
  auto* heap_data = (IRDescriptorTableEntry*)heap->contents();
  
  XELOGI("=== {} Descriptor Heap Dump ===", name);
  for (size_t i = 0; i < 32; ++i) {
    if (heap_data[i].gpuVA != 0 || 
        heap_data[i].textureViewID != 0 || 
        heap_data[i].metadata != 0) {
      XELOGI("  [{}]: gpuVA=0x{:016x} tex=0x{:016x} meta=0x{:016x}", 
             i, 
             heap_data[i].gpuVA,
             heap_data[i].textureViewID,
             heap_data[i].metadata);
    }
  }
  XELOGI("=== End Heap Dump ===");
}

void MetalDebugUtils::DumpPipelineState(MTL::RenderPipelineState* pipeline) {
  if (!dump_state_ || !pipeline) return;
  
  // Get pipeline label if available
  NS::String* label = pipeline->label();
  std::string label_str = label ? label->utf8String() : "unlabeled";
  
  XELOGI("Pipeline State: {}", label_str);
  // Additional pipeline state could be dumped here
}

void MetalDebugUtils::DumpViewportScissor(RegisterFile* register_file) {
  if (!dump_state_ || !register_file) return;
  
  // Viewport
  float vp_xscale = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_XSCALE]);
  float vp_yscale = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_YSCALE]);
  float vp_xoffset = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_XOFFSET]);
  float vp_yoffset = *reinterpret_cast<float*>(&register_file->values[XE_GPU_REG_PA_CL_VPORT_YOFFSET]);
  
  XELOGI("Viewport: scale=({}, {}), offset=({}, {})",
         vp_xscale, vp_yscale, vp_xoffset, vp_yoffset);
  
  // Scissor
  uint32_t scissor_tl = register_file->values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL];
  uint32_t scissor_br = register_file->values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];
  
  uint32_t sx = scissor_tl & 0x7FFF;
  uint32_t sy = (scissor_tl >> 16) & 0x7FFF;
  uint32_t ex = scissor_br & 0x7FFF;
  uint32_t ey = (scissor_br >> 16) & 0x7FFF;
  
  XELOGI("Scissor: ({}, {}) to ({}, {}) = {}x{}", 
         sx, sy, ex, ey, ex - sx, ey - sy);
}

void MetalDebugUtils::DumpRenderTargets() {
  if (!dump_state_) return;
  
  XELOGI("DumpRenderTargets - implementation pending");
  // Would need access to render target cache
}

std::string MetalDebugUtils::FormatTextureLabel(uint32_t width, uint32_t height,
                                                uint32_t format, uint32_t slot) {
  return fmt::format("Tex_{}x{}_fmt{}_slot{}", width, height, format, slot);
}

std::string MetalDebugUtils::FormatPipelineLabel(uint64_t vs_hash, uint64_t ps_hash) {
  return fmt::format("PSO_VS{:016X}_PS{:016X}", vs_hash, ps_hash);
}

bool MetalDebugUtils::CheckTextureIsEmpty(MTL::Texture* texture) {
  if (!texture) return true;
  
  // Sample a few pixels to check if texture has content
  // This is a quick check, not exhaustive
  size_t width = texture->width();
  size_t height = texture->height();
  
  if (width == 0 || height == 0) return true;
  
  // Check center pixel
  uint32_t pixel[4] = {0};
  texture->getBytes(pixel, 16, 
                    MTL::Region::Make2D(width/2, height/2, 1, 1), 0);
  
  return (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0);
}

void MetalDebugUtils::LogStatistics() {
  XELOGI("=== Metal Debug Statistics ===");
  XELOGI("  Zero scissor draws: {}", zero_scissor_count_);
  XELOGI("  Invalid viewport draws: {}", invalid_viewport_count_);
  XELOGI("  Empty descriptor heaps: {}", empty_descriptor_count_);
  XELOGI("  Null texture bindings: {}", null_texture_count_);
  XELOGI("  GPU captures taken: {}", captured_count_);
  XELOGI("==============================");
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe