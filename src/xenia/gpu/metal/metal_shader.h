/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_SHADER_H_
#define XENIA_GPU_METAL_METAL_SHADER_H_

#include <memory>
#include <string>
#include <vector>

#include "xenia/gpu/dxbc_shader.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/xenos.h"

#include "third_party/metal-shader-converter/include/metal_irconverter.h"
#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalShader : public DxbcShader {
 public:
  // Resource mapping from reflection data
  struct ResourceMapping {
    uint32_t hlsl_slot;          // HLSL register slot (t0, s0, b0, etc.)
    uint32_t hlsl_space;         // HLSL register space
    uint32_t resource_type;      // IRResourceType from metal_irconverter.h
    size_t arg_buffer_offset;    // Offset in argument buffer (in entries, not bytes!)
  };
  
  // Top-level argument buffer entry from reflection
  struct ABEntry {
    uint32_t elt_offset;     // Offset in bytes
    enum class Kind { SRV, UAV, CBV, Sampler, Unknown } kind;
    uint32_t slot;           // Reflection "Slot"
    uint32_t space;          // Reflection "Space"
    std::string name;        // Resource name for logging
  };

  class MetalTranslation : public DxbcTranslation {
   public:
    MetalTranslation(MetalShader& shader, uint64_t modification);
    ~MetalTranslation() override;

    // Convert DXIL to Metal (called by the Metal backend when needed)
    bool ConvertToMetal();

    // Get the compiled Metal library for pipeline creation
    MTL::Library* GetMetalLibrary() const { return metal_library_; }
    MTL::Function* GetMetalFunction() const { return metal_function_; }
    
    // Get resource mappings from reflection data
    const std::vector<ResourceMapping>& GetResourceMappings() const { 
      return resource_mappings_; 
    }
    
    // Get texture and sampler slots from reflection
    const std::vector<uint32_t>& GetTextureSlots() const { return texture_slots_; }
    const std::vector<uint32_t>& GetSamplerSlots() const { return sampler_slots_; }
    
    // Get top-level argument buffer layout from reflection
    const std::vector<ABEntry>& GetTopLevelABLayout() const { return top_level_ab_layout_; }

   private:
    MetalShader& metal_shader_;

    // Metal Shader Converter objects
    IRCompiler* ir_compiler_ = nullptr;
    IRObject* ir_object_ = nullptr;
    IRMetalLibBinary* metal_lib_binary_ = nullptr;
    
    // Metal library and function (using metal-cpp)
    MTL::Library* metal_library_ = nullptr;
    MTL::Function* metal_function_ = nullptr;
    
    // Resource mappings from shader reflection
    std::vector<ResourceMapping> resource_mappings_;
    
    // Texture and sampler slots extracted from reflection
    std::vector<uint32_t> texture_slots_;
    std::vector<uint32_t> sampler_slots_;
    
    // Top-level argument buffer layout from reflection
    std::vector<ABEntry> top_level_ab_layout_;

    // Convert DXIL bytecode to Metal IR using Metal Shader Converter
    bool ConvertDxilToMetal(const std::vector<uint8_t>& dxil_bytecode);
    
    // Create Metal library from Metal IR
    bool CreateMetalLibrary();
    
    // Capture reflection data after compilation
    bool CaptureReflectionData();
    
    // Parse texture bindings from JSON reflection data
    void ParseTextureBindingsFromJSON(const std::string& json_str);
    
    // Create and set root signature for consistent resource layout
    bool CreateAndSetRootSignature();
  };

  MetalShader(xenos::ShaderType shader_type, uint64_t data_hash,
              const uint32_t* dword_ptr, uint32_t dword_count);
  ~MetalShader() override;

  // Create a new translation for the given modification
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  // Metal-specific shader configuration
  void ConfigureMetalShaderConverter();
};

// Cleanup function to be called on shutdown
void CleanupMetalShaderResources();

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_H_
