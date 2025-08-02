/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_
#define XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_

#include <memory>
#include <unordered_map>

#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

#if XE_PLATFORM_MAC
#ifdef METAL_CPP_AVAILABLE
#include "third_party/metal-cpp/Metal/Metal.hpp"
#endif  // METAL_CPP_AVAILABLE
#endif  // XE_PLATFORM_MAC

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;
class MetalShader;

class MetalPipelineCache {
 public:
  MetalPipelineCache(MetalCommandProcessor* command_processor,
                     const RegisterFile* register_file,
                     bool edram_rov_used);
  ~MetalPipelineCache();

  bool Initialize();
  void Shutdown();
  void ClearCache();

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Get or create Metal render pipeline state
  MTL::RenderPipelineState* GetRenderPipelineState(
      const RenderPipelineDescription& description);
  
  // Get or create Metal compute pipeline state
  MTL::ComputePipelineState* GetComputePipelineState(
      const ComputePipelineDescription& description);
#endif

 protected:
  bool TranslateShader(DxbcShaderTranslator& translator, 
                       const Shader& shader, 
                       reg::SQ_PROGRAM_CNTL cntl);

 private:
  struct RenderPipelineDescription {
    uint64_t vertex_shader_hash;
    uint64_t pixel_shader_hash;
    uint64_t vertex_shader_modification;
    uint64_t pixel_shader_modification;
    
    // Render state from Xbox 360 GPU registers
    uint32_t color_mask[4];
    uint32_t blend_enable;
    uint32_t blend_func;
    uint32_t depth_func;
    bool depth_write_enable;
    bool stencil_enable;
    
    // Vertex layout and format information
    uint32_t vertex_format_hash;
    
    bool operator==(const RenderPipelineDescription& other) const;
  };

  struct RenderPipelineDescriptionHasher {
    size_t operator()(const RenderPipelineDescription& desc) const;
  };

  struct ComputePipelineDescription {
    uint64_t shader_hash;
    uint64_t shader_modification;
    
    bool operator==(const ComputePipelineDescription& other) const;
  };

  struct ComputePipelineDescriptionHasher {
    size_t operator()(const ComputePipelineDescription& desc) const;
  };

  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  bool edram_rov_used_;

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Metal pipeline state caches
  std::unordered_map<RenderPipelineDescription, MTL::RenderPipelineState*,
                     RenderPipelineDescriptionHasher> render_pipeline_cache_;
  
  std::unordered_map<ComputePipelineDescription, MTL::ComputePipelineState*,
                     ComputePipelineDescriptionHasher> compute_pipeline_cache_;

  // Helper methods for Metal pipeline creation
  MTL::RenderPipelineState* CreateRenderPipelineState(
      const RenderPipelineDescription& description);
  
  MTL::ComputePipelineState* CreateComputePipelineState(
      const ComputePipelineDescription& description);
  
  // Convert Xbox 360 blend state to Metal
  void ConfigureBlendState(MTL::RenderPipelineDescriptor* descriptor,
                           const RenderPipelineDescription& description);
  
  // Convert Xbox 360 vertex format to Metal
  MTL::VertexDescriptor* CreateVertexDescriptor(
      const RenderPipelineDescription& description);
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  // Shader compilation helpers
  bool CompileMetalShader(MetalShader* shader, uint64_t modification);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_
