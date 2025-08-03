/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_pipeline_cache.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_shader.h"

namespace xe {
namespace gpu {
namespace metal {

MetalPipelineCache::MetalPipelineCache(MetalCommandProcessor* command_processor,
                                       const RegisterFile* register_file,
                                       bool edram_rov_used)
    : command_processor_(command_processor),
      register_file_(register_file),
      edram_rov_used_(edram_rov_used) {}

MetalPipelineCache::~MetalPipelineCache() {
  Shutdown();
}

bool MetalPipelineCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  XELOGI("Metal pipeline cache: Initialized successfully");
  return true;
}

void MetalPipelineCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

  XELOGI("Metal pipeline cache: Shutdown complete");
}

void MetalPipelineCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

  // Release all Metal render pipeline states
  for (auto& pair : render_pipeline_cache_) {
    if (pair.second) {
      pair.second->release();
    }
  }
  render_pipeline_cache_.clear();

  // Release all Metal compute pipeline states
  for (auto& pair : compute_pipeline_cache_) {
    if (pair.second) {
      pair.second->release();
    }
  }
  compute_pipeline_cache_.clear();

  XELOGI("Metal pipeline cache: Cache cleared");
}

bool MetalPipelineCache::TranslateShader(DxbcShaderTranslator& translator,
                                          const Shader& shader,
                                          reg::SQ_PROGRAM_CNTL cntl) {
  SCOPE_profile_cpu_f("gpu");

  // Metal-specific shader translation will be implemented here
  // This will use Metal Shader Converter for Xbox 360 game shaders
  XELOGI("Metal pipeline cache: Shader translation requested for shader %016" PRIx64, 
         shader.ucode_data_hash());
  
  // For now, just return true as a stub
  return true;
}

Shader* MetalPipelineCache::LoadShader(xenos::ShaderType shader_type,
                                       const uint32_t* host_address,
                                       uint32_t dword_count) {
  SCOPE_profile_cpu_f("gpu");

  // Phase B Step 1: Use MetalShader for DXIL→Metal conversion
  XELOGI("Metal pipeline cache: Loading {} shader, {} dwords",
         shader_type == xenos::ShaderType::kVertex ? "vertex" : "pixel",
         dword_count);

  // Create a hash from the shader data using XXH3 (same as D3D12 backend)
  uint64_t data_hash = XXH3_64bits(host_address, dword_count * sizeof(uint32_t));
  
  XELOGI("Metal pipeline cache: Shader data hash = {:016x}", data_hash);

  // Create MetalShader object which supports DXIL→Metal conversion
  auto metal_shader = std::make_unique<MetalShader>(shader_type, data_hash, host_address, dword_count);
  
  // Analyze the shader ucode to populate basic information
  StringBuffer ucode_disasm_buffer;
  metal_shader->AnalyzeUcode(ucode_disasm_buffer);
  
  XELOGI("Metal pipeline cache: Created MetalShader object for {} shader",
         shader_type == xenos::ShaderType::kVertex ? "vertex" : "pixel");
  
  // For Phase B, we'll start the DXIL translation process
  // The actual Metal compilation will happen when needed by the pipeline
  return metal_shader.release();
}

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineState(
    const RenderPipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");

  // Check if pipeline already exists in cache
  auto it = render_pipeline_cache_.find(description);
  if (it != render_pipeline_cache_.end()) {
    return it->second;
  }

  // Create new pipeline state
  MTL::RenderPipelineState* pipeline_state = CreateRenderPipelineState(description);
  if (pipeline_state) {
    render_pipeline_cache_[description] = pipeline_state;
    XELOGI("Metal pipeline cache: Created new render pipeline (total: {})",
           render_pipeline_cache_.size());
  }

  return pipeline_state;
}

MTL::ComputePipelineState* MetalPipelineCache::GetComputePipelineState(
    const ComputePipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");

  // Check if pipeline already exists in cache
  auto it = compute_pipeline_cache_.find(description);
  if (it != compute_pipeline_cache_.end()) {
    return it->second;
  }

  // Create new pipeline state
  MTL::ComputePipelineState* pipeline_state = CreateComputePipelineState(description);
  if (pipeline_state) {
    compute_pipeline_cache_[description] = pipeline_state;
    XELOGI("Metal pipeline cache: Created new compute pipeline (total: {})",
           compute_pipeline_cache_.size());
  }

  return pipeline_state;
}

MTL::RenderPipelineState* MetalPipelineCache::CreateRenderPipelineState(
    const RenderPipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal pipeline cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  // Create render pipeline descriptor
  MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
  if (!descriptor) {
    XELOGE("Metal pipeline cache: Failed to create render pipeline descriptor");
    device->release();
    return nullptr;
  }

  // Phase B Step 3: Set vertex and fragment functions from shader cache
  // For now, we'll create basic placeholder functions to pass Metal validation
  // In later phases, this will use the actual compiled Metal shaders
  
  // Get Metal library and create basic vertex/fragment functions
  std::string vertex_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
};

struct VertexOut {
    float4 position [[position]];
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 1.0);
    return out;
}
)";

  std::string fragment_source = R"(
#include <metal_stdlib>
using namespace metal;

fragment float4 fragment_main() {
    return float4(1.0, 0.0, 1.0, 1.0); // Magenta placeholder
}
)";

  // Create temporary Metal library for placeholder shaders
  NS::String* vertex_code = NS::String::string(vertex_source.c_str(), NS::UTF8StringEncoding);
  MTL::CompileOptions* compile_options = MTL::CompileOptions::alloc()->init();
  
  NS::Error* error = nullptr;
  MTL::Library* library = device->newLibrary(vertex_code, compile_options, &error);
  
  if (error || !library) {
    XELOGW("Metal pipeline cache: Failed to create placeholder shader library");
    if (error) {
      XELOGW("Metal pipeline cache: Error: {}", error->localizedDescription()->utf8String());
    }
    // Continue without shaders for now - this will fail validation but we can debug
  } else {
    // Get vertex function
    NS::String* vertex_func_name = NS::String::string("vertex_main", NS::UTF8StringEncoding);
    MTL::Function* vertex_function = library->newFunction(vertex_func_name);
    if (vertex_function) {
      descriptor->setVertexFunction(vertex_function);
      vertex_function->release();
      XELOGI("Metal pipeline cache: Set placeholder vertex function");
    }
    
    // Get fragment function  
    NS::String* fragment_func_name = NS::String::string("fragment_main", NS::UTF8StringEncoding);
    MTL::Function* fragment_function = library->newFunction(fragment_func_name);
    if (fragment_function) {
      descriptor->setFragmentFunction(fragment_function);
      fragment_function->release();
      XELOGI("Metal pipeline cache: Set placeholder fragment function");
    }
    
    library->release();
  }
  
  compile_options->release();
  vertex_code->release();
  
  // Configure blend state
  ConfigureBlendState(descriptor, description);

  // Configure vertex descriptor
  MTL::VertexDescriptor* vertex_descriptor = CreateVertexDescriptor(description);
  if (vertex_descriptor) {
    descriptor->setVertexDescriptor(vertex_descriptor);
    vertex_descriptor->release();
  }

  // Set render target formats (simplified for now)
  descriptor->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  // TODO: Add depth attachment when implementing Xbox 360 depth buffer support
  // descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

  // Create the pipeline state
  error = nullptr;
  MTL::RenderPipelineState* pipeline_state = device->newRenderPipelineState(descriptor, &error);
  
  descriptor->release();
  device->release();

  if (!pipeline_state) {
    NS::String* error_desc = error ? error->localizedDescription() 
                                   : NS::String::string("Unknown error", NS::UTF8StringEncoding);
    XELOGE("Metal pipeline cache: Failed to create render pipeline state: {}", 
           error_desc->utf8String());
    return nullptr;
  }

  return pipeline_state;
}

MTL::ComputePipelineState* MetalPipelineCache::CreateComputePipelineState(
    const ComputePipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");

  // TODO: Implement compute pipeline creation
  XELOGE("Metal pipeline cache: Compute pipeline creation not yet implemented");
  return nullptr;
}

void MetalPipelineCache::ConfigureBlendState(
    MTL::RenderPipelineDescriptor* descriptor,
    const RenderPipelineDescription& description) {
  // Configure blend state for color attachment 0
  MTL::RenderPipelineColorAttachmentDescriptor* color_attachment = 
      descriptor->colorAttachments()->object(0);

  // Set color write mask
  uint32_t write_mask = MTL::ColorWriteMaskNone;
  if (description.color_mask[0] & 0x1) write_mask |= MTL::ColorWriteMaskRed;
  if (description.color_mask[0] & 0x2) write_mask |= MTL::ColorWriteMaskGreen;
  if (description.color_mask[0] & 0x4) write_mask |= MTL::ColorWriteMaskBlue;
  if (description.color_mask[0] & 0x8) write_mask |= MTL::ColorWriteMaskAlpha;
  color_attachment->setWriteMask(static_cast<MTL::ColorWriteMask>(write_mask));

  // Enable blending if needed
  if (description.blend_enable) {
    color_attachment->setBlendingEnabled(true);
    
    // TODO: Convert Xbox 360 blend functions to Metal blend operations
    // For now, use basic alpha blending
    color_attachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    color_attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    color_attachment->setRgbBlendOperation(MTL::BlendOperationAdd);
    
    color_attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    color_attachment->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);
    color_attachment->setAlphaBlendOperation(MTL::BlendOperationAdd);
  } else {
    color_attachment->setBlendingEnabled(false);
  }
}

MTL::VertexDescriptor* MetalPipelineCache::CreateVertexDescriptor(
    const RenderPipelineDescription& description) {
  MTL::VertexDescriptor* vertex_descriptor = MTL::VertexDescriptor::alloc()->init();
  if (!vertex_descriptor) {
    return nullptr;
  }

  // Phase B Step 3: Configure basic vertex descriptor for placeholder shaders
  // This matches our placeholder vertex shader that expects position at attribute 0
  
  // Configure position attribute (float3 at attribute 0)
  MTL::VertexAttributeDescriptor* position_attr = vertex_descriptor->attributes()->object(0);
  position_attr->setFormat(MTL::VertexFormatFloat3);
  position_attr->setOffset(0);
  position_attr->setBufferIndex(0);
  
  // Configure buffer layout (stride = 3 floats = 12 bytes)
  MTL::VertexBufferLayoutDescriptor* buffer_layout = vertex_descriptor->layouts()->object(0);
  buffer_layout->setStride(12); // 3 * sizeof(float)
  buffer_layout->setStepRate(1);
  buffer_layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
  
  XELOGI("Metal pipeline cache: Created basic vertex descriptor with position attribute");
  
  // TODO: Configure vertex attributes based on actual Xbox 360 vertex format
  // This requires parsing the vertex format hash and setting up appropriate
  // Metal vertex attributes and buffer layouts for real Xbox 360 data
  
  return vertex_descriptor;
}

bool MetalPipelineCache::CompileMetalShader(MetalShader* shader, uint64_t modification) {
  if (!shader) {
    return false;
  }

  // Get or create the translation for this modification
  auto translation = shader->GetOrCreateTranslation(modification);
  if (!translation) {
    XELOGE("Metal pipeline cache: Failed to get shader translation");
    return false;
  }

  // Check if translation is already prepared
  if (translation->is_translated() && translation->is_valid()) {
    return true;
  }

  // TODO: Trigger Metal shader compilation if not already done
  // This will involve calling the Metal Shader Converter APIs
  
  return translation->is_valid();
}

// Pipeline description comparison operators
bool MetalPipelineCache::RenderPipelineDescription::operator==(
    const RenderPipelineDescription& other) const {
  return vertex_shader_hash == other.vertex_shader_hash &&
         pixel_shader_hash == other.pixel_shader_hash &&
         vertex_shader_modification == other.vertex_shader_modification &&
         pixel_shader_modification == other.pixel_shader_modification &&
         std::memcmp(color_mask, other.color_mask, sizeof(color_mask)) == 0 &&
         blend_enable == other.blend_enable &&
         blend_func == other.blend_func &&
         depth_func == other.depth_func &&
         depth_write_enable == other.depth_write_enable &&
         stencil_enable == other.stencil_enable &&
         vertex_format_hash == other.vertex_format_hash;
}

size_t MetalPipelineCache::RenderPipelineDescriptionHasher::operator()(
    const RenderPipelineDescription& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint64_t>{}(desc.vertex_shader_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.pixel_shader_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.vertex_shader_modification) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.pixel_shader_modification) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.vertex_format_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  return hash;
}

bool MetalPipelineCache::ComputePipelineDescription::operator==(
    const ComputePipelineDescription& other) const {
  return shader_hash == other.shader_hash &&
         shader_modification == other.shader_modification;
}

size_t MetalPipelineCache::ComputePipelineDescriptionHasher::operator()(
    const ComputePipelineDescription& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint64_t>{}(desc.shader_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.shader_modification) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  return hash;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
