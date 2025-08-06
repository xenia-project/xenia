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
#include "xenia/gpu/metal/metal_shader.h"
#include "xenia/ui/graphics_provider.h"


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

  // Initialize shader translator for Xbox 360 → DXIL conversion
  shader_translator_ = std::make_unique<DxbcShaderTranslator>(
      ui::GraphicsProvider::GpuVendorID::kApple,  // vendor_id - use Apple for Metal
      false,  // bindless_resources_used
      edram_rov_used_,
      false,  // gamma_render_target_as_srgb
      true,   // msaa_2x_supported 
      1,      // draw_resolution_scale_x
      1,      // draw_resolution_scale_y
      false  // force_emit_source_map
  );

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

  // Check if shader already exists in cache
  auto it = shaders_.find(data_hash);
  if (it != shaders_.end()) {
    XELOGI("Metal pipeline cache: Found existing shader in cache");
    return it->second.get();
  }

  // Create MetalShader object which supports DXIL→Metal conversion
  auto metal_shader = std::make_unique<MetalShader>(shader_type, data_hash, host_address, dword_count);
  
  // Analyze the shader ucode to populate basic information
  StringBuffer ucode_disasm_buffer;
  metal_shader->AnalyzeUcode(ucode_disasm_buffer);
  
  XELOGI("Metal pipeline cache: Created MetalShader object for {} shader",
         shader_type == xenos::ShaderType::kVertex ? "vertex" : "pixel");
  
  // For Phase B, we'll start the DXIL translation process
  // The actual Metal compilation will happen when needed by the pipeline
  MetalShader* shader_ptr = metal_shader.get();
  shaders_[data_hash] = std::move(metal_shader);
  
  return shader_ptr;
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

  // Phase D.1 Step 1: Get actual Xbox 360 shaders and convert to Metal
  // Replace placeholder shaders with real DXIL→Metal conversion
  
  // Look up shaders by hash in the shader cache
  MetalShader* vertex_shader = nullptr;
  MetalShader* pixel_shader = nullptr;
  
  // Find vertex shader in cache
  for (auto& shader_entry : shaders_) {
    auto* shader = shader_entry.second.get();
    if (shader->ucode_data_hash() == description.vertex_shader_hash && 
        shader->type() == xenos::ShaderType::kVertex) {
      vertex_shader = static_cast<MetalShader*>(shader);
      break;
    }
  }
  
  // Find pixel shader in cache
  for (auto& shader_entry : shaders_) {
    auto* shader = shader_entry.second.get();
    if (shader->ucode_data_hash() == description.pixel_shader_hash && 
        shader->type() == xenos::ShaderType::kPixel) {
      pixel_shader = static_cast<MetalShader*>(shader);
      break;
    }
  }
  
  if (!vertex_shader || !pixel_shader) {
    XELOGE("Metal pipeline cache: Missing vertex ({}) or pixel ({}) shader for pipeline creation",
           vertex_shader ? "found" : "missing", pixel_shader ? "found" : "missing");
    descriptor->release();
    return nullptr;
  }
  
  // Phase D.1 Step 2: Convert Xbox 360 shaders to Metal using Metal Shader Converter
  XELOGI("Metal pipeline cache: Converting Xbox 360 shaders to Metal...");
  XELOGI("Metal pipeline cache: Vertex shader hash: {:016x}, Pixel shader hash: {:016x}",
         vertex_shader->ucode_data_hash(), pixel_shader->ucode_data_hash());
  
  // First, ensure Xbox 360 → DXIL translation has happened
  XELOGI("Metal pipeline cache: Getting vertex shader translation...");
  auto vertex_translation = vertex_shader->GetOrCreateTranslation(0);
  if (!vertex_translation->is_translated()) {
    XELOGI("Metal pipeline cache: Vertex shader not yet translated, translating Xbox 360 → DXBC...");
    StringBuffer ucode_disasm_buffer;
    vertex_shader->AnalyzeUcode(ucode_disasm_buffer);
    if (!TranslateAnalyzedShader(vertex_shader)) {
      XELOGE("Metal pipeline cache: Failed to translate vertex shader from Xbox 360 to DXIL");
      descriptor->release();
      return nullptr;
    }
    XELOGI("Metal pipeline cache: Vertex shader Xbox 360 → DXBC translation complete");
  } else {
    XELOGI("Metal pipeline cache: Vertex shader already translated to DXBC");
  }
  if (!vertex_translation->is_valid()) {
    XELOGE("Metal pipeline cache: Vertex shader translation is not valid");
    descriptor->release();
    return nullptr;
  }
  
  XELOGI("Metal pipeline cache: Getting pixel shader translation...");
  auto pixel_translation_default = pixel_shader->GetOrCreateTranslation(0);
  if (!pixel_translation_default->is_translated()) {
    XELOGI("Metal pipeline cache: Pixel shader not yet translated, translating Xbox 360 → DXBC...");
    StringBuffer ucode_disasm_buffer;
    pixel_shader->AnalyzeUcode(ucode_disasm_buffer);
    if (!TranslateAnalyzedShader(pixel_shader)) {
      XELOGE("Metal pipeline cache: Failed to translate pixel shader from Xbox 360 to DXIL");
      descriptor->release();
      return nullptr;
    }
    XELOGI("Metal pipeline cache: Pixel shader Xbox 360 → DXBC translation complete");
  } else {
    XELOGI("Metal pipeline cache: Pixel shader already translated to DXBC");
  }
  if (!pixel_translation_default->is_valid()) {
    XELOGE("Metal pipeline cache: Pixel shader translation is not valid");
    descriptor->release();
    return nullptr;
  }
  
  XELOGI("Metal pipeline cache: Xbox 360 → DXIL translation completed for both shaders");
  
  // Get or create DXIL translation for vertex shader
  vertex_translation = vertex_shader->GetOrCreateTranslation(description.vertex_shader_modification);
  if (!vertex_translation) {
    XELOGE("Metal pipeline cache: Failed to get vertex shader translation");
    descriptor->release();
    return nullptr;
  }
  
  // Convert to Metal
  XELOGI("Metal pipeline cache: Converting vertex shader DXBC → DXIL → Metal...");
  auto* vertex_metal_translation = static_cast<MetalShader::MetalTranslation*>(vertex_translation);
  if (!vertex_metal_translation->ConvertToMetal()) {
    XELOGE("Metal pipeline cache: Failed to convert vertex shader to Metal");
    descriptor->release();
    return nullptr;
  }
  XELOGI("Metal pipeline cache: Vertex shader successfully converted to Metal");
  
  // Get or create DXIL translation for pixel shader
  auto pixel_translation = pixel_shader->GetOrCreateTranslation(description.pixel_shader_modification);
  if (!pixel_translation) {
    XELOGE("Metal pipeline cache: Failed to get pixel shader translation");
    descriptor->release();
    return nullptr;
  }
  
  // Convert to Metal
  XELOGI("Metal pipeline cache: Converting pixel shader DXBC → DXIL → Metal...");
  auto* pixel_metal_translation = static_cast<MetalShader::MetalTranslation*>(pixel_translation);
  if (!pixel_metal_translation->ConvertToMetal()) {
    XELOGE("Metal pipeline cache: Failed to convert pixel shader to Metal");
    descriptor->release();
    return nullptr;
  }
  XELOGI("Metal pipeline cache: Pixel shader successfully converted to Metal");
  
  // Phase D.1 Step 3: Get Metal functions from converted shaders
  MTL::Function* vertex_function = vertex_metal_translation->GetMetalFunction();
  MTL::Function* fragment_function = pixel_metal_translation->GetMetalFunction();
  
  if (!vertex_function || !fragment_function) {
    XELOGE("Metal pipeline cache: Failed to get Metal functions from converted shaders");
    descriptor->release();
    return nullptr;
  }
  
  // Set the real Metal functions
  descriptor->setVertexFunction(vertex_function);
  descriptor->setFragmentFunction(fragment_function);
  
  XELOGI("Metal pipeline cache: Successfully set Xbox 360→Metal converted shader functions");
  
  // Configure blend state
  ConfigureBlendState(descriptor, description);

  // Configure vertex descriptor
  MTL::VertexDescriptor* vertex_descriptor = CreateVertexDescriptor(description);
  if (vertex_descriptor) {
    descriptor->setVertexDescriptor(vertex_descriptor);
    vertex_descriptor->release();
  }

  // Set render target formats from the pipeline description
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    if (description.color_formats[i] != MTL::PixelFormatInvalid) {
      descriptor->colorAttachments()->object(i)->setPixelFormat(description.color_formats[i]);
    }
  }
  
  // Set depth format if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    descriptor->setDepthAttachmentPixelFormat(description.depth_format);
  }
  
  // Set sample count for MSAA
  if (description.sample_count > 1) {
    descriptor->setSampleCount(description.sample_count);
  }

    // Create the pipeline state
  NS::Error* error = nullptr;
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

bool MetalPipelineCache::TranslateAnalyzedShader(MetalShader* shader) {
  if (!shader) {
    return false;
  }

  XELOGI("Metal pipeline cache: TranslateAnalyzedShader called for {} shader (hash: {:016x})",
         shader->type() == xenos::ShaderType::kVertex ? "vertex" : "pixel",
         shader->ucode_data_hash());

  // Create a translation with default modification (0)
  auto translation = shader->GetOrCreateTranslation(0);
  if (!translation) {
    XELOGE("Metal pipeline cache: Failed to create shader translation");
    return false;
  }

  // Use the DXBC shader translator to convert Xbox 360 → DXIL
  XELOGI("Metal pipeline cache: Calling DxbcShaderTranslator to convert Xbox 360 microcode → DXBC...");
  if (!shader_translator_->TranslateAnalyzedShader(*translation)) {
    XELOGE("Metal pipeline cache: Failed to translate Xbox 360 shader to DXIL");
    return false;
  }

  XELOGI("Metal pipeline cache: DxbcShaderTranslator succeeded, translation valid: {}", 
         translation->is_valid());
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
         vertex_format_hash == other.vertex_format_hash &&
         std::memcmp(color_formats, other.color_formats, sizeof(color_formats)) == 0 &&
         depth_format == other.depth_format &&
         num_color_attachments == other.num_color_attachments &&
         sample_count == other.sample_count;
}

size_t MetalPipelineCache::RenderPipelineDescriptionHasher::operator()(
    const RenderPipelineDescription& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint64_t>{}(desc.vertex_shader_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.pixel_shader_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.vertex_shader_modification) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint64_t>{}(desc.pixel_shader_modification) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.vertex_format_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.num_color_attachments) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  for (uint32_t i = 0; i < desc.num_color_attachments; ++i) {
    hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(desc.color_formats[i])) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(desc.depth_format)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.sample_count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
