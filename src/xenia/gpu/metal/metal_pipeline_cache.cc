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
#include <map>
#include <set>
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
  // CachedPipeline destructor handles cleanup
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
    return it->second->pipeline_state;
  }

  // Create new pipeline state
  MTL::RenderPipelineState* pipeline_state = CreateRenderPipelineState(description);
  if (pipeline_state) {
    // Note: CreateRenderPipelineState now handles cache insertion
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
  
  // DEBUG: Check if vertex shader texture bindings are populated after translation
  const auto& vs_texture_bindings = vertex_shader->texture_bindings();
  XELOGI("Metal pipeline cache: Vertex shader after translation has {} texture bindings", vs_texture_bindings.size());
  for (size_t i = 0; i < vs_texture_bindings.size(); ++i) {
    XELOGI("  VS texture binding[{}]: fetch_constant={}", 
           i, vs_texture_bindings[i].fetch_constant);
  }
  
  // Convert to Metal
  XELOGI("Metal pipeline cache: Converting vertex shader DXBC → DXIL → Metal...");
  auto* vertex_metal_translation = static_cast<MetalShader::MetalTranslation*>(vertex_translation);
  if (!vertex_metal_translation->ConvertToMetal()) {
    XELOGE("Metal pipeline cache: Failed to convert vertex shader to Metal");
    descriptor->release();
    return nullptr;
  }

  // NEW: Augment texture bindings with MSC reflection data
  AugmentTextureBindings(vertex_shader, vertex_metal_translation);

  XELOGI("Metal pipeline cache: Vertex shader successfully converted to Metal");
  
  // Get or create DXIL translation for pixel shader
  auto pixel_translation = pixel_shader->GetOrCreateTranslation(description.pixel_shader_modification);
  if (!pixel_translation) {
    XELOGE("Metal pipeline cache: Failed to get pixel shader translation");
    descriptor->release();
    return nullptr;
  }
  
  // DEBUG: Check if pixel shader texture bindings are populated after translation
  const auto& ps_texture_bindings = pixel_shader->texture_bindings();
  XELOGI("Metal pipeline cache: Pixel shader after translation has {} texture bindings", ps_texture_bindings.size());
  for (size_t i = 0; i < ps_texture_bindings.size(); ++i) {
    XELOGI("  PS texture binding[{}]: fetch_constant={}", 
           i, ps_texture_bindings[i].fetch_constant);
  }
  
  // Convert to Metal
  XELOGI("Metal pipeline cache: Converting pixel shader DXBC → DXIL → Metal...");
  auto* pixel_metal_translation = static_cast<MetalShader::MetalTranslation*>(pixel_translation);
  if (!pixel_metal_translation->ConvertToMetal()) {
    XELOGE("Metal pipeline cache: Failed to convert pixel shader to Metal");
    descriptor->release();
    return nullptr;
  }

  // NEW: Augment texture bindings with MSC reflection data
  AugmentTextureBindings(pixel_shader, pixel_metal_translation);

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
  
  // Retain functions for creating debug variants
  vertex_function->retain();
  fragment_function->retain();
  
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
    
    // If depth format includes stencil, set stencil attachment format too
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      descriptor->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  // Set sample count for MSAA (always set it, even if 1)
  // This ensures the pipeline and render targets have matching sample counts
  descriptor->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Set input primitive topology based on the primitive type
  // This tells Metal how to assemble vertices into primitives
  MTL::PrimitiveTopologyClass topology_class = MTL::PrimitiveTopologyClassTriangle;
  switch (description.primitive_type) {
    case xenos::PrimitiveType::kPointList:
      topology_class = MTL::PrimitiveTopologyClassPoint;
      break;
    case xenos::PrimitiveType::kLineList:
    case xenos::PrimitiveType::kLineStrip:
    case xenos::PrimitiveType::kLineLoop:
      topology_class = MTL::PrimitiveTopologyClassLine;
      break;
    case xenos::PrimitiveType::kTriangleList:
    case xenos::PrimitiveType::kTriangleStrip:
    case xenos::PrimitiveType::kTriangleFan:
      topology_class = MTL::PrimitiveTopologyClassTriangle;
      break;
    default:
      // Default to triangles for unknown types
      topology_class = MTL::PrimitiveTopologyClassTriangle;
      break;
  }
  descriptor->setInputPrimitiveTopology(topology_class);

  // CRITICAL FIX: Ensure rasterization is enabled (Metal enables it by default, but be explicit)
  descriptor->setRasterizationEnabled(true);
  
  // CRITICAL FIX: Force all color writes to be enabled
  // This ensures fragments are written to the render target
  for (uint32_t i = 0; i < 4; ++i) {
    auto* color_attachment = descriptor->colorAttachments()->object(i);
    color_attachment->setWriteMask(MTL::ColorWriteMaskAll);
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
    vertex_function->release();
    fragment_function->release();
    return nullptr;
  }

  // Store in cache with functions
  auto cached = std::make_unique<CachedPipeline>();
  cached->pipeline_state = pipeline_state;
  cached->vertex_function = vertex_function;
  cached->fragment_function = fragment_function;
  render_pipeline_cache_[description] = std::move(cached);

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
  // TEMPORARY: Force all color writes on to test if mask is the issue
  color_attachment->setWriteMask(MTL::ColorWriteMaskAll);
  /*
  uint32_t write_mask = MTL::ColorWriteMaskNone;
  if (description.color_mask[0] & 0x1) write_mask |= MTL::ColorWriteMaskRed;
  if (description.color_mask[0] & 0x2) write_mask |= MTL::ColorWriteMaskGreen;
  if (description.color_mask[0] & 0x4) write_mask |= MTL::ColorWriteMaskBlue;
  if (description.color_mask[0] & 0x8) write_mask |= MTL::ColorWriteMaskAlpha;
  color_attachment->setWriteMask(static_cast<MTL::ColorWriteMask>(write_mask));
  */

  // TEMPORARY: Disable blending to test if it's causing black output
  color_attachment->setBlendingEnabled(false);
  /*
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
  */
}

MTL::VertexDescriptor* MetalPipelineCache::CreateVertexDescriptor(
    const RenderPipelineDescription& description) {
  MTL::VertexDescriptor* vertex_descriptor = MTL::VertexDescriptor::alloc()->init();
  if (!vertex_descriptor) {
    return nullptr;
  }

  // Find the vertex shader to get its vertex bindings
  MetalShader* vertex_shader = nullptr;
  for (auto& shader_entry : shaders_) {
    auto* shader = shader_entry.second.get();
    if (shader->ucode_data_hash() == description.vertex_shader_hash && 
        shader->type() == xenos::ShaderType::kVertex) {
      vertex_shader = static_cast<MetalShader*>(shader);
      break;
    }
  }
  
  if (!vertex_shader) {
    XELOGE("Metal pipeline cache: No vertex shader found for vertex descriptor creation");
    // Fall back to a simple descriptor
    // Attributes start at index 11 for Metal IR Converter
    MTL::VertexAttributeDescriptor* attr0 = vertex_descriptor->attributes()->object(11);  // kIRStageInAttributeStartIndex
    attr0->setFormat(MTL::VertexFormatFloat3);
    attr0->setOffset(0);
    attr0->setBufferIndex(6);  // kIRVertexBufferBindPoint = 6
    
    MTL::VertexBufferLayoutDescriptor* buffer_layout = vertex_descriptor->layouts()->object(6);
    buffer_layout->setStride(12);
    buffer_layout->setStepRate(1);
    buffer_layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
    
    return vertex_descriptor;
  }
  
  // Build vertex descriptor from shader's vertex bindings
  const auto& vertex_bindings = vertex_shader->vertex_bindings();
  XELOGI("Metal pipeline cache: Building vertex descriptor from {} vertex bindings", 
         vertex_bindings.size());
  
  // Track which buffer indices actually have attributes
  std::set<uint32_t> used_buffer_indices;
  std::map<uint32_t, uint32_t> buffer_strides;  // buffer_index -> stride
  
  // Attributes must start at index 11 for Metal IR Converter
  uint32_t attribute_index = 11;  // kIRStageInAttributeStartIndex
  for (const auto& binding : vertex_bindings) {
    // Metal IR Converter expects vertex buffers starting at index 6
    uint32_t buffer_index = 6 + binding.binding_index;  // kIRVertexBufferBindPoint = 6
    
    // Track buffer info, but don't configure layout yet
    buffer_strides[buffer_index] = binding.stride_words * 4;
    
    XELOGI("  Binding {}: buffer index {}, stride {} bytes", 
           binding.binding_index, buffer_index, binding.stride_words * 4);
    
    // Process each attribute in this binding
    bool has_valid_attributes = false;
    for (const auto& attribute : binding.attributes) {
      const auto& fetch = attribute.fetch_instr;
      
      // Map Xbox 360 vertex format to Metal format
      MTL::VertexFormat metal_format = MTL::VertexFormatInvalid;
      switch (fetch.attributes.data_format) {
        case xenos::VertexFormat::k_32_FLOAT:
          metal_format = MTL::VertexFormatFloat;
          break;
        case xenos::VertexFormat::k_32_32_FLOAT:
          metal_format = MTL::VertexFormatFloat2;
          break;
        case xenos::VertexFormat::k_32_32_32_FLOAT:
          metal_format = MTL::VertexFormatFloat3;
          break;
        case xenos::VertexFormat::k_32_32_32_32_FLOAT:
          metal_format = MTL::VertexFormatFloat4;
          break;
        case xenos::VertexFormat::k_16_16_FLOAT:
          metal_format = MTL::VertexFormatHalf2;
          break;
        case xenos::VertexFormat::k_16_16_16_16_FLOAT:
          metal_format = MTL::VertexFormatHalf4;
          break;
        case xenos::VertexFormat::k_8_8_8_8:
          metal_format = MTL::VertexFormatUChar4Normalized;
          break;
        default:
          XELOGW("    Unsupported vertex format: {}", 
                 static_cast<uint32_t>(fetch.attributes.data_format));
          continue;
      }
      
      if (metal_format != MTL::VertexFormatInvalid && attribute_index < 31) {
        MTL::VertexAttributeDescriptor* attr = 
            vertex_descriptor->attributes()->object(attribute_index);
        attr->setFormat(metal_format);
        attr->setOffset(fetch.attributes.offset * 4);  // Convert words to bytes
        attr->setBufferIndex(buffer_index);
        
        XELOGI("    Attribute {}: format {}, offset {} bytes, buffer {}", 
               attribute_index, static_cast<uint32_t>(metal_format), 
               fetch.attributes.offset * 4, buffer_index);
        
        // Mark this buffer as used since it has a valid attribute
        used_buffer_indices.insert(buffer_index);
        has_valid_attributes = true;
        
        attribute_index++;
      }
    }
    
    // Only configure buffer layout if it has valid attributes
    if (has_valid_attributes) {
      MTL::VertexBufferLayoutDescriptor* buffer_layout = 
          vertex_descriptor->layouts()->object(buffer_index);
      buffer_layout->setStride(buffer_strides[buffer_index]);
      buffer_layout->setStepRate(1);
      buffer_layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
      
      XELOGI("  Configured layout for buffer {}: stride {} bytes", 
             buffer_index, buffer_strides[buffer_index]);
    } else {
      XELOGI("  Skipping layout for buffer {} (no valid attributes)", buffer_index);
    }
  }
  
  XELOGI("Metal pipeline cache: Created vertex descriptor with {} attributes", 
         attribute_index);
  
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

void MetalPipelineCache::AugmentTextureBindings(MetalShader* shader, 
                                                MetalShader::MetalTranslation* translation) {
  // Get MSC's understanding of required texture slots
  const auto& msc_texture_slots = translation->GetTextureSlots();
  
  // Get mutable access to shader's texture bindings
  auto& shader_bindings = shader->mutable_texture_bindings();
  
  XELOGI("Metal: Augmenting texture bindings - MSC reports {} slots, shader has {} bindings",
         msc_texture_slots.size(), shader_bindings.size());
  
  // Ensure we have bindings for all MSC-identified texture slots
  for (size_t slot_index = 0; slot_index < msc_texture_slots.size(); ++slot_index) {
    uint32_t heap_slot = msc_texture_slots[slot_index];
    
    // Skip sentinel values (0xFFFF indicates framebuffer fetch)
    if (heap_slot == 0xFFFF) {
      XELOGI("  Skipping sentinel at slot {}", slot_index);
      continue;
    }
    
    // Check if we already have a binding for this fetch constant
    bool found = false;
    for (const auto& binding : shader_bindings) {
      if (binding.fetch_constant == slot_index) {
        found = true;
        XELOGI("  Slot {} already has binding (fetch_constant={})", 
               slot_index, binding.fetch_constant);
        break;
      }
    }
    
    if (!found) {
      // Get actual fetch constant data from Xbox 360 registers
      xenos::xe_gpu_texture_fetch_t fetch = register_file_->GetTextureFetch(static_cast<uint32_t>(slot_index));
      
      // CRITICAL: Always create synthetic binding even if there's no texture data
      // The null texture system will handle empty slots in the command processor
      // MSC expects bindings for ALL declared texture slots, not just populated ones
      
      // Create synthetic binding with fetch constant data (or defaults if empty)
      Shader::TextureBinding new_binding = {};
      new_binding.fetch_constant = static_cast<uint32_t>(slot_index);
      new_binding.binding_index = shader_bindings.size();
      
      // Populate the fetch instruction with actual data from the fetch constant
      auto& fetch_instr = new_binding.fetch_instr;
      
      // Set dimension based on fetch type
      switch (fetch.dimension) {
        case xenos::DataDimension::k1D:
          fetch_instr.dimension = xenos::FetchOpDimension::k1D;
          break;
        case xenos::DataDimension::k2DOrStacked:
          // For stacked textures, use k3DOrStacked, otherwise k2D
          fetch_instr.dimension = fetch.stacked ? 
            xenos::FetchOpDimension::k3DOrStacked : xenos::FetchOpDimension::k2D;
          break;
        case xenos::DataDimension::k3D:
          fetch_instr.dimension = xenos::FetchOpDimension::k3DOrStacked;
          break;
        case xenos::DataDimension::kCube:
          fetch_instr.dimension = xenos::FetchOpDimension::kCube;
          break;
        default:
          fetch_instr.dimension = xenos::FetchOpDimension::k2D;  // Default to 2D
          break;
      }
      
      // Set opcode for texture fetch
      fetch_instr.opcode = ucode::FetchOpcode::kTextureFetch;
      fetch_instr.opcode_name = "tfetch";
      
      // Set texture attributes based on fetch constant
      fetch_instr.attributes.mag_filter = fetch.mag_filter;
      fetch_instr.attributes.min_filter = fetch.min_filter;
      fetch_instr.attributes.mip_filter = fetch.mip_filter;
      fetch_instr.attributes.aniso_filter = fetch.aniso_filter;
      fetch_instr.attributes.vol_mag_filter = static_cast<xenos::TextureFilter>(fetch.vol_mag_filter);
      fetch_instr.attributes.vol_min_filter = static_cast<xenos::TextureFilter>(fetch.vol_min_filter);
      fetch_instr.attributes.use_computed_lod = true;
      fetch_instr.attributes.use_register_lod = false;
      fetch_instr.attributes.use_register_gradients = false;
      fetch_instr.attributes.lod_bias = 0.0f;
      fetch_instr.attributes.offset_x = 0.0f;
      fetch_instr.attributes.offset_y = 0.0f;
      
      shader_bindings.push_back(new_binding);
      if (fetch.base_address) {
        XELOGI("  Added synthetic binding for slot {} (fetch_constant={}, base_address=0x{:08X}, dimension={})", 
               slot_index, new_binding.fetch_constant, fetch.base_address, 
               static_cast<uint32_t>(fetch_instr.dimension));
      } else {
        XELOGI("  Added synthetic binding for EMPTY slot {} (fetch_constant={}, no texture data - will use null texture)", 
               slot_index, new_binding.fetch_constant);
      }
    }
  }
  
  // Log final state of all texture bindings after augmentation
  XELOGI("Metal: After augmentation, shader has {} texture bindings:",
         shader_bindings.size());
  for (size_t i = 0; i < shader_bindings.size(); ++i) {
    const auto& binding = shader_bindings[i];
    // Try to get fetch constant data to see if it's valid
    xenos::xe_gpu_texture_fetch_t fetch = register_file_->GetTextureFetch(binding.fetch_constant);
    XELOGI("  Binding[{}]: fetch_constant={}, base_address=0x{:08X}, dimension={}",
           i, binding.fetch_constant, fetch.base_address, 
           static_cast<uint32_t>(binding.fetch_instr.dimension));
  }
  
  // CRITICAL: If using root signature, update the texture slots to match augmented bindings
  // Root signature mode creates manual texture slots based on shader bindings,
  // but that happens BEFORE augmentation. We need to update them after.
  if (shader_bindings.size() > msc_texture_slots.size()) {
    std::vector<uint32_t> updated_slots;
    for (size_t i = 0; i < shader_bindings.size(); i++) {
      updated_slots.push_back(static_cast<uint32_t>(i));
    }
    translation->UpdateTextureSlots(updated_slots);
    XELOGI("Metal: Updated texture slots for root signature mode - now {} slots", updated_slots.size());
  }
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

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineStateWithRedPS(
    const RenderPipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");
  
  // First ensure the normal pipeline exists and is cached
  auto it = render_pipeline_cache_.find(description);
  if (it == render_pipeline_cache_.end()) {
    // Create the normal pipeline first
    GetRenderPipelineState(description);
    it = render_pipeline_cache_.find(description);
    if (it == render_pipeline_cache_.end()) {
      XELOGE("Metal pipeline cache: Failed to create base pipeline for red PS variant");
      return nullptr;
    }
  }
  
  auto* cached = it->second.get();
  if (!cached || !cached->vertex_function) {
    XELOGE("Metal pipeline cache: No vertex function available for red PS variant");
    return nullptr;
  }
  
  // Create a simple red fragment shader
  static const char* kRedPS = R"(
    #include <metal_stdlib>
    using namespace metal;
    fragment float4 debug_fragment_red() { 
      return float4(1.0, 0.0, 0.0, 1.0);  // Solid red
    }
  )";
  
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal pipeline cache: No Metal device for red PS variant");
    return nullptr;
  }
  
  // Compile red fragment shader
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  MTL::Library* library = device->newLibrary(
    NS::String::string(kRedPS, NS::UTF8StringEncoding),
    options,
    &error
  );
  
  if (!library) {
    XELOGE("Metal pipeline cache: Failed to compile red fragment shader: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    return nullptr;
  }
  
  MTL::Function* red_function = library->newFunction(
    NS::String::string("debug_fragment_red", NS::UTF8StringEncoding)
  );
  
  if (!red_function) {
    XELOGE("Metal pipeline cache: Failed to get red fragment function");
    library->release();
    options->release();
    return nullptr;
  }
  
  // Create pipeline descriptor with real VS and red PS
  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(cached->vertex_function);
  desc->setFragmentFunction(red_function);
  
  // Copy render target configuration from original
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    if (description.color_formats[i] != MTL::PixelFormatInvalid) {
      auto* ca = desc->colorAttachments()->object(i);
      ca->setPixelFormat(description.color_formats[i]);
      ca->setWriteMask(MTL::ColorWriteMaskAll);  // Force all writes
      ca->setBlendingEnabled(false);
    }
  }
  
  // Set depth format if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    desc->setDepthAttachmentPixelFormat(description.depth_format);
    
    // If depth format includes stencil, set stencil attachment format too
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  // Set sample count
  desc->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Set input primitive topology - required for Metal to assemble vertices correctly
  // Default to triangles as that's the most common
  desc->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassTriangle);
  
  // Configure vertex descriptor (same as original)
  MTL::VertexDescriptor* vertex_descriptor = CreateVertexDescriptor(description);
  if (vertex_descriptor) {
    desc->setVertexDescriptor(vertex_descriptor);
    vertex_descriptor->release();
  }
  
  // Create the pipeline
  MTL::RenderPipelineState* red_pipeline = device->newRenderPipelineState(desc, &error);
  
  if (!red_pipeline) {
    XELOGE("Metal pipeline cache: Failed to create red PS pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
  } else {
    XELOGI("Metal pipeline cache: Created real VS + red PS debug pipeline");
  }
  
  // Clean up
  if (error) error->release();
  desc->release();
  red_function->release();
  library->release();
  options->release();
  
  return red_pipeline;
}

std::pair<MetalShader::MetalTranslation*, MetalShader::MetalTranslation*>
MetalPipelineCache::GetShaderTranslations(const RenderPipelineDescription& description) {
  // Find the shaders in cache
  auto vertex_it = shaders_.find(description.vertex_shader_hash);
  auto pixel_it = shaders_.find(description.pixel_shader_hash);
  
  if (vertex_it == shaders_.end() || pixel_it == shaders_.end()) {
    XELOGE("Metal pipeline cache: Shaders not found for translation retrieval");
    return {nullptr, nullptr};
  }
  
  MetalShader* vertex_shader = static_cast<MetalShader*>(vertex_it->second.get());
  MetalShader* pixel_shader = static_cast<MetalShader*>(pixel_it->second.get());
  
  // Get the translations with the appropriate modifications
  auto vertex_translation = static_cast<MetalShader::MetalTranslation*>(
      vertex_shader->GetOrCreateTranslation(description.vertex_shader_modification));
  auto pixel_translation = static_cast<MetalShader::MetalTranslation*>(
      pixel_shader->GetOrCreateTranslation(description.pixel_shader_modification));
  
  return {vertex_translation, pixel_translation};
}

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineStateWithMinimalVSRealPS(
    const RenderPipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");
  
  // First ensure the normal pipeline exists and is cached
  auto it = render_pipeline_cache_.find(description);
  if (it == render_pipeline_cache_.end()) {
    // Create the normal pipeline first
    GetRenderPipelineState(description);
    it = render_pipeline_cache_.find(description);
    if (it == render_pipeline_cache_.end()) {
      XELOGE("Metal pipeline cache: Failed to create base pipeline for minimal VS variant");
      return nullptr;
    }
  }
  
  auto* cached = it->second.get();
  if (!cached || !cached->fragment_function) {
    XELOGE("Metal pipeline cache: No fragment function available for minimal VS variant");
    return nullptr;
  }
  
  // Create a minimal vertex shader that outputs a fullscreen triangle in clip space
  static const char* kMinimalVS = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
      float2 texcoord;
    };
    
    vertex VertexOut debug_vertex_minimal(uint vid [[vertex_id]]) {
      VertexOut out;
      // Generate fullscreen triangle
      float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
      };
      float2 texcoords[3] = {
        float2(0.0, 1.0),
        float2(2.0, 1.0),
        float2(0.0, -1.0)
      };
      
      out.position = float4(positions[vid], 0.0, 1.0);
      out.texcoord = texcoords[vid];
      return out;
    }
  )";
  
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal pipeline cache: No Metal device for minimal VS variant");
    return nullptr;
  }
  
  // Compile minimal vertex shader
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  MTL::Library* library = device->newLibrary(
      NS::String::string(kMinimalVS, NS::UTF8StringEncoding),
      options, &error);
  
  if (!library) {
    XELOGE("Metal pipeline cache: Failed to compile minimal VS: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    return nullptr;
  }
  
  MTL::Function* minimal_vs = library->newFunction(NS::String::string("debug_vertex_minimal", NS::UTF8StringEncoding));
  if (!minimal_vs) {
    XELOGE("Metal pipeline cache: Failed to get minimal VS function");
    library->release();
    options->release();
    return nullptr;
  }
  
  // Create pipeline descriptor with minimal VS + real PS
  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(minimal_vs);
  desc->setFragmentFunction(cached->fragment_function);  // Use real PS
  
  // Configure color attachments (same as original)
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    color_attachment->setPixelFormat(description.color_formats[i]);
    // TODO: Set blend state based on description
  }
  
  // Configure depth attachment if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    desc->setDepthAttachmentPixelFormat(description.depth_format);
    
    // If depth format includes stencil, set stencil attachment format too
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  // Set sample count
  desc->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Set input primitive topology based on the primitive type
  MTL::PrimitiveTopologyClass topology_class = MTL::PrimitiveTopologyClassTriangle;
  switch (description.primitive_type) {
    case xenos::PrimitiveType::kPointList:
      topology_class = MTL::PrimitiveTopologyClassPoint;
      break;
    case xenos::PrimitiveType::kLineList:
    case xenos::PrimitiveType::kLineStrip:
    case xenos::PrimitiveType::kLineLoop:
      topology_class = MTL::PrimitiveTopologyClassLine;
      break;
    default:
      topology_class = MTL::PrimitiveTopologyClassTriangle;
      break;
  }
  desc->setInputPrimitiveTopology(topology_class);
  
  // No vertex descriptor needed for minimal VS
  
  // Create the pipeline
  MTL::RenderPipelineState* minimal_pipeline = device->newRenderPipelineState(desc, &error);
  
  if (!minimal_pipeline) {
    XELOGE("Metal pipeline cache: Failed to create minimal VS pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
  } else {
    XELOGI("Metal pipeline cache: Created minimal VS + real PS debug pipeline");
  }
  
  // Clean up
  minimal_vs->release();
  options->release();
  library->release();
  desc->release();
  if (error) error->release();
  
  return minimal_pipeline;
}

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineStateWithViewportShim(
    const RenderPipelineDescription& description) {
  SCOPE_profile_cpu_f("gpu");
  
  // First ensure the normal pipeline exists and is cached
  auto it = render_pipeline_cache_.find(description);
  if (it == render_pipeline_cache_.end()) {
    // Create the normal pipeline first
    GetRenderPipelineState(description);
    it = render_pipeline_cache_.find(description);
    if (it == render_pipeline_cache_.end()) {
      XELOGE("Metal pipeline cache: Failed to create base pipeline for viewport shim");
      return nullptr;
    }
  }
  
  auto* cached = it->second.get();
  if (!cached || !cached->vertex_function || !cached->fragment_function) {
    XELOGE("Metal pipeline cache: No functions available for viewport shim");
    return nullptr;
  }
  
  // Get the original vertex shader's metallib
  MetalShader* vertex_shader = static_cast<MetalShader*>(
      shaders_.find(description.vertex_shader_hash)->second.get());
  if (!vertex_shader) {
    XELOGE("Metal pipeline cache: Failed to find vertex shader for shim");
    return nullptr;
  }
  
  auto* vertex_translation = static_cast<MetalShader::MetalTranslation*>(
      vertex_shader->GetOrCreateTranslation(description.vertex_shader_modification));
  if (!vertex_translation) {
    XELOGE("Metal pipeline cache: No vertex translation for shim");
    return nullptr;
  }
  
  // Create a shim vertex shader that wraps the original and transforms coordinates
  // This shader will:
  // 1. Call the original vertex shader
  // 2. Transform the output from screen space to clip space
  static const char* kViewportShimVS = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    // Match the original VS output structure
    struct VertexOut {
      float4 position [[position]];
      // Additional interpolants would go here
    };
    
    // System constants we inject at CB0
    struct SystemConstants {
      float2 ndc_scale;      // (2.0/width, -2.0/height)
      float2 ndc_offset;     // (-1.0, 1.0)
      float2 rt_size;        // (width, height)
      float2 rt_size_inv;    // (1.0/width, 1.0/height)
      float4 viewport_scale; // From registers
      float4 viewport_offset;
    };
    
    vertex VertexOut viewport_shim_vs(
        uint vertex_id [[vertex_id]],
        uint instance_id [[instance_id]],
        constant SystemConstants& sys [[buffer(6)]]  // Our uniforms buffer
    ) {
      VertexOut out;
      
      // Generate a large triangle that covers the whole screen
      // Already in clip space coordinates
      float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
      };
      
      // Already in clip space, just use directly
      float2 clip_xy = positions[vertex_id % 3];
      
      out.position = float4(clip_xy, 0.0, 1.0);
      
      return out;
    }
  )";
  
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal pipeline cache: No Metal device for viewport shim");
    return nullptr;
  }
  
  // Compile shim vertex shader
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  MTL::Library* shim_library = device->newLibrary(
      NS::String::string(kViewportShimVS, NS::UTF8StringEncoding),
      options, &error);
  
  if (!shim_library) {
    XELOGE("Metal pipeline cache: Failed to compile viewport shim: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    return nullptr;
  }
  
  MTL::Function* shim_vs = shim_library->newFunction(
      NS::String::string("viewport_shim_vs", NS::UTF8StringEncoding));
  if (!shim_vs) {
    XELOGE("Metal pipeline cache: Failed to get viewport shim function");
    shim_library->release();
    options->release();
    return nullptr;
  }
  
  // Create pipeline descriptor with shim VS + real PS
  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(shim_vs);
  desc->setFragmentFunction(cached->fragment_function);  // Use the real fragment shader
  
  // Configure color attachments (same as original)
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    color_attachment->setPixelFormat(description.color_formats[i]);
  }
  
  // Configure depth attachment if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    desc->setDepthAttachmentPixelFormat(description.depth_format);
    
    // If depth format includes stencil, set stencil attachment format too
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  // Set sample count
  desc->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Create the pipeline
  MTL::RenderPipelineState* shim_pipeline = device->newRenderPipelineState(desc, &error);
  
  if (!shim_pipeline) {
    XELOGE("Metal pipeline cache: Failed to create viewport shim pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
  } else {
    XELOGI("Metal pipeline cache: Created viewport transform shim pipeline");
  }
  
  // Clean up
  shim_vs->release();
  options->release();
  shim_library->release();
  desc->release();
  if (error) error->release();
  
  return shim_pipeline;
}

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineStateWithGreenPS(
    const RenderPipelineDescription& description) {
  XELOGI("Metal pipeline cache: Creating debug pipeline with real VS + solid green PS");
  
  // Find the cached pipeline with the real vertex shader
  auto it = render_pipeline_cache_.find(description);
  if (it == render_pipeline_cache_.end()) {
    XELOGE("Metal pipeline cache: No cached pipeline found for green PS variant");
    return nullptr;
  }
  
  auto* cached = it->second.get();
  if (!cached || !cached->vertex_function) {
    XELOGE("Metal pipeline cache: No vertex function available for green PS variant");
    return nullptr;
  }
  
  MTL::Device* device = command_processor_->GetMetalDevice();
  
  // Create a simple solid green fragment shader
  static const char* kGreenPS = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct FragmentOut {
      float4 color [[color(0)]];
    };
    
    fragment FragmentOut test_fragment_green() {
      FragmentOut out;
      out.color = float4(0.0, 1.0, 0.0, 1.0);  // Solid green
      return out;
    }
  )";
  
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  
  MTL::Library* ps_library = device->newLibrary(
      NS::String::string(kGreenPS, NS::UTF8StringEncoding),
      options, &error);
  
  if (!ps_library) {
    XELOGE("Metal pipeline cache: Failed to compile green PS: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    return nullptr;
  }
  
  MTL::Function* green_ps = ps_library->newFunction(
      NS::String::string("test_fragment_green", NS::UTF8StringEncoding));
  
  if (!green_ps) {
    XELOGE("Metal pipeline cache: Failed to get green PS function");
    ps_library->release();
    options->release();
    return nullptr;
  }
  
  // Create pipeline descriptor with real VS + green PS
  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(cached->vertex_function);
  desc->setFragmentFunction(green_ps);
  
  // Configure color attachments (same as original)
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    color_attachment->setPixelFormat(description.color_formats[i]);
    // Force color writes on for testing
    color_attachment->setWriteMask(MTL::ColorWriteMaskAll);
  }
  
  // Configure depth attachment if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    desc->setDepthAttachmentPixelFormat(description.depth_format);
    
    // If depth format includes stencil, set stencil attachment format too
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  // CRITICAL: Set sample count to match render target
  desc->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Create the pipeline
  MTL::RenderPipelineState* green_pipeline = device->newRenderPipelineState(desc, &error);
  
  if (!green_pipeline) {
    XELOGE("Metal pipeline cache: Failed to create green PS pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
  } else {
    XELOGI("Metal pipeline cache: Created green PS debug pipeline successfully");
  }
  
  // Clean up
  green_ps->release();
  ps_library->release();
  options->release();
  desc->release();
  if (error) error->release();
  
  return green_pipeline;
}

MTL::RenderPipelineState* MetalPipelineCache::GetRenderPipelineStateWithNDCTransform(
    const RenderPipelineDescription& description) {
  XELOGI("Metal pipeline cache: Creating NDC transformation test pipeline");
  
  // Get the normal pipeline to ensure shaders are compiled
  auto it = render_pipeline_cache_.find(description);
  if (it == render_pipeline_cache_.end()) {
    GetRenderPipelineState(description);
    it = render_pipeline_cache_.find(description);
    if (it == render_pipeline_cache_.end()) {
      XELOGE("Metal pipeline cache: Failed to create base pipeline for NDC transform");
      return nullptr;
    }
  }
  
  auto* cached = it->second.get();
  if (!cached || !cached->fragment_function) {
    XELOGE("Metal pipeline cache: No fragment function for NDC transform pipeline");
    return nullptr;
  }
  
  MTL::Device* device = command_processor_->GetMetalDevice();
  
  // Create a test vertex shader that applies NDC transformation
  // This reads position from vertex buffer and transforms it using CB0 constants
  static const char* kNDCTransformVS = R"(
    #include <metal_stdlib>
    #include <metal_matrix>
    using namespace metal;
    
    // Match MSC's runtime structure
    struct IRDescriptorTableEntry {
      uint64_t gpuVA;
      uint32_t textureViewID;
      uint32_t metadata;
    };
    
    // Vertex output structure
    struct VertexOut {
      float4 position [[position]];
      float4 color;
    };
    
    // System constants from CB0 (injected by command processor)
    struct SystemConstants {
      float4 ndc_scale;     // CB0[0]: (x_scale, y_scale, z_scale, unused)
      float4 ndc_offset;    // CB0[1]: (x_offset, y_offset, z_offset, unused)
      float4 viewport_dims; // CB0[2]: (width, height, 1/width, 1/height)
    };
    
    vertex VertexOut ndc_transform_vs(
        uint vertex_id [[vertex_id]],
        constant IRDescriptorTableEntry* top_level_ab [[buffer(2)]],
        constant void* uniforms [[buffer(6)]]  // Direct uniforms buffer access
    ) {
      VertexOut out;
      
      // Access system constants from the uniforms buffer at CB0
      constant SystemConstants* sys = (constant SystemConstants*)uniforms;
      
      // Generate test triangle vertices in screen space (Xbox 360 style)
      float2 screen_positions[3] = {
        float2(100.0, 100.0),   // Top-left in screen space
        float2(700.0, 100.0),   // Top-right 
        float2(400.0, 500.0)    // Bottom-center
      };
      
      float2 screen_pos = screen_positions[vertex_id % 3];
      
      // Apply NDC transformation using the injected constants
      // Transform from Xbox 360 screen space to Metal NDC
      float3 ndc_pos;
      ndc_pos.x = screen_pos.x * sys->ndc_scale.x + sys->ndc_offset.x;
      ndc_pos.y = screen_pos.y * sys->ndc_scale.y + sys->ndc_offset.y;
      ndc_pos.z = 0.0; // Keep at near plane for now
      
      out.position = float4(ndc_pos, 1.0);
      
      // Debug colors for each vertex
      if (vertex_id % 3 == 0) {
        out.color = float4(1.0, 0.0, 0.0, 1.0); // Red
      } else if (vertex_id % 3 == 1) {
        out.color = float4(0.0, 1.0, 0.0, 1.0); // Green
      } else {
        out.color = float4(0.0, 0.0, 1.0, 1.0); // Blue
      }
      
      return out;
    }
  )";
  
  // Create a simple pass-through fragment shader that uses vertex colors
  static const char* kPassthroughPS = R"(
    #include <metal_stdlib>
    using namespace metal;
    
    struct VertexOut {
      float4 position [[position]];
      float4 color;
    };
    
    fragment float4 passthrough_ps(VertexOut in [[stage_in]]) {
      return in.color; // Use vertex color
    }
  )";
  
  NS::Error* error = nullptr;
  MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
  
  // Compile vertex shader
  MTL::Library* vs_library = device->newLibrary(
      NS::String::string(kNDCTransformVS, NS::UTF8StringEncoding),
      options, &error);
  
  if (!vs_library) {
    XELOGE("Metal pipeline cache: Failed to compile NDC transform VS: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    return nullptr;
  }
  
  MTL::Function* ndc_vs = vs_library->newFunction(
      NS::String::string("ndc_transform_vs", NS::UTF8StringEncoding));
  
  // Compile fragment shader
  MTL::Library* ps_library = device->newLibrary(
      NS::String::string(kPassthroughPS, NS::UTF8StringEncoding),
      options, &error);
  
  if (!ps_library) {
    XELOGE("Metal pipeline cache: Failed to compile passthrough PS: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    if (error) error->release();
    options->release();
    vs_library->release();
    return nullptr;
  }
  
  MTL::Function* passthrough_ps = ps_library->newFunction(
      NS::String::string("passthrough_ps", NS::UTF8StringEncoding));
  
  if (!ndc_vs || !passthrough_ps) {
    XELOGE("Metal pipeline cache: Failed to get NDC transform functions");
    vs_library->release();
    ps_library->release();
    options->release();
    return nullptr;
  }
  
  // Create pipeline descriptor
  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(ndc_vs);
  desc->setFragmentFunction(passthrough_ps);
  
  // Configure color attachments
  for (uint32_t i = 0; i < description.num_color_attachments; ++i) {
    auto* color_attachment = desc->colorAttachments()->object(i);
    color_attachment->setPixelFormat(description.color_formats[i]);
  }
  
  // Configure depth if present
  if (description.depth_format != MTL::PixelFormatInvalid) {
    desc->setDepthAttachmentPixelFormat(description.depth_format);
    if (description.depth_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        description.depth_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
      desc->setStencilAttachmentPixelFormat(description.depth_format);
    }
  }
  
  desc->setSampleCount(description.sample_count ? description.sample_count : 1);
  
  // Create the pipeline
  MTL::RenderPipelineState* ndc_pipeline = device->newRenderPipelineState(desc, &error);
  
  if (!ndc_pipeline) {
    XELOGE("Metal pipeline cache: Failed to create NDC transform pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
  } else {
    XELOGI("Metal pipeline cache: Successfully created NDC transformation test pipeline");
    XELOGI("  - Will transform screen space vertices to NDC using CB0 constants");
    XELOGI("  - Should render a colored triangle if NDC transform works");
  }
  
  // Clean up
  desc->release();
  ndc_vs->release();
  passthrough_ps->release();
  vs_library->release();
  ps_library->release();
  options->release();
  if (error) error->release();
  
  return ndc_pipeline;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
