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

#include "xenia/base/hash.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

#if XE_PLATFORM_MAC
#include "third_party/metal-cpp/Metal/Metal.hpp"
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

  // Shader loading and management
  Shader* LoadShader(xenos::ShaderType shader_type,
                     const uint32_t* host_address, uint32_t dword_count);

  // Pipeline state descriptions (public for use by command processor)
  struct RenderPipelineDescription {
    uint64_t vertex_shader_hash;
    uint64_t pixel_shader_hash;
    uint64_t vertex_shader_modification;
    uint64_t pixel_shader_modification;
    
    // Primitive type for this draw call
    xenos::PrimitiveType primitive_type;
    
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

  // Get or create Metal render pipeline state
  MTL::RenderPipelineState* GetRenderPipelineState(
      const RenderPipelineDescription& description);
  
  // Get or create Metal compute pipeline state
  MTL::ComputePipelineState* GetComputePipelineState(
      const ComputePipelineDescription& description);

 protected:
  bool TranslateShader(DxbcShaderTranslator& translator, 
                       const Shader& shader, 
                       reg::SQ_PROGRAM_CNTL cntl);

 private:
  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  bool edram_rov_used_;

  // Shader translator for Xbox 360 → DXIL conversion
  std::unique_ptr<DxbcShaderTranslator> shader_translator_;

  // Metal pipeline state caches
  std::unordered_map<RenderPipelineDescription, MTL::RenderPipelineState*,
                     RenderPipelineDescriptionHasher> render_pipeline_cache_;
  
  std::unordered_map<ComputePipelineDescription, MTL::ComputePipelineState*,
                     ComputePipelineDescriptionHasher> compute_pipeline_cache_;

  // Shader cache - maps shader hash to shader object
  std::unordered_map<uint64_t, std::unique_ptr<MetalShader>, xe::hash::IdentityHasher<uint64_t>>
      shaders_;

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

  // Shader compilation helpers
  bool CompileMetalShader(MetalShader* shader, uint64_t modification);
  
  // Translate Xbox 360 shader to DXIL
  bool TranslateAnalyzedShader(MetalShader* shader);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PIPELINE_CACHE_H_
