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
#include <utility>

#include "xenia/base/hash.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/gpu/metal/metal_shader.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

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
    
    // Render target formats (up to 4 color attachments + depth)
    MTL::PixelFormat color_formats[4];
    MTL::PixelFormat depth_format;
    uint32_t num_color_attachments;
    uint32_t sample_count;  // MSAA sample count
    
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
  
  // Get shader translations for accessing reflection data
  // Returns pair of <vertex_translation, pixel_translation>
  std::pair<MetalShader::MetalTranslation*, MetalShader::MetalTranslation*> 
      GetShaderTranslations(const RenderPipelineDescription& description);
  
  // Get debug pipeline with real vertex shader but red fragment shader
  MTL::RenderPipelineState* GetRenderPipelineStateWithRedPS(
      const RenderPipelineDescription& description);
  
  // Get debug pipeline with minimal VS (fullscreen tri) but real PS  
  MTL::RenderPipelineState* GetRenderPipelineStateWithMinimalVSRealPS(
      const RenderPipelineDescription& description);
  
  // Get pipeline with viewport transform shim wrapping the real VS
  MTL::RenderPipelineState* GetRenderPipelineStateWithViewportShim(
      const RenderPipelineDescription& description);
  
  // Get debug pipeline with real VS but solid green PS (no textures)
  MTL::RenderPipelineState* GetRenderPipelineStateWithGreenPS(
      const RenderPipelineDescription& description);

 protected:
  bool TranslateShader(DxbcShaderTranslator& translator, 
                       const Shader& shader, 
                       reg::SQ_PROGRAM_CNTL cntl);

 private:
  MetalCommandProcessor* command_processor_;
  const RegisterFile* register_file_;
  bool edram_rov_used_;

  // Cached pipeline with vertex function for debug variants
  struct CachedPipeline {
    MTL::RenderPipelineState* pipeline_state;
    MTL::Function* vertex_function;  // Retained for creating debug variants
    MTL::Function* fragment_function;  // Retained for debugging
    
    CachedPipeline() : pipeline_state(nullptr), vertex_function(nullptr), fragment_function(nullptr) {}
    ~CachedPipeline() {
      if (pipeline_state) pipeline_state->release();
      if (vertex_function) vertex_function->release();
      if (fragment_function) fragment_function->release();
    }
  };

  // Shader translator for Xbox 360 → DXIL conversion
  std::unique_ptr<DxbcShaderTranslator> shader_translator_;

  // Metal pipeline state caches
  std::unordered_map<RenderPipelineDescription, std::unique_ptr<CachedPipeline>,
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
