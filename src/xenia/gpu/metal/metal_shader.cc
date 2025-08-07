/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader.h"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/metal/dxbc_to_dxil_converter.h"
#include "xenia/gpu/metal/metal_shader_cache.h"

// Define metal-cpp implementation (only in one cpp file)
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "third_party/metal-cpp/Metal/Metal.hpp"

// Include dispatch for GCD functions
#include <dispatch/dispatch.h>

// Include Metal Shader Converter headers
#include "third_party/metal-shader-converter/include/metal_irconverter.h"
// Include runtime header for IRDescriptorTableEntry
// Define IR_RUNTIME_METALCPP to use metal-cpp types
#define IR_RUNTIME_METALCPP 1
#include "/usr/local/include/metal_irconverter_runtime/metal_irconverter_runtime.h"

namespace xe {
namespace gpu {
namespace metal {

// Global DXBC to DXIL converter instance (initialized once)
static std::unique_ptr<DxbcToDxilConverter> g_dxbc_to_dxil_converter;

MetalShader::MetalShader(xenos::ShaderType shader_type, uint64_t data_hash,
                         const uint32_t* dword_ptr, uint32_t dword_count)
    : DxbcShader(shader_type, data_hash, dword_ptr, dword_count) {
  ConfigureMetalShaderConverter();
}

MetalShader::~MetalShader() = default;

Shader::Translation* MetalShader::CreateTranslationInstance(
    uint64_t modification) {
  return new MetalTranslation(*this, modification);
}

void MetalShader::ConfigureMetalShaderConverter() {
  // Configure Metal Shader Converter for Xbox 360 GPU compatibility
  // TODO: Add specific configuration for Xbox 360 features:
  // - Primitive restart handling
  // - Vertex fetch configuration
  // - Resource binding model
  // - Texture array requirements
}

MetalShader::MetalTranslation::MetalTranslation(MetalShader& shader,
                                                 uint64_t modification)
    : DxbcTranslation(shader, modification), metal_shader_(shader) {}

MetalShader::MetalTranslation::~MetalTranslation() {
  // Clean up Metal objects (metal-cpp uses RAII)
  if (metal_function_) {
    metal_function_->release();
    metal_function_ = nullptr;
  }
  if (metal_library_) {
    metal_library_->release();
    metal_library_ = nullptr;
  }

  // Clean up Metal Shader Converter objects
  if (metal_lib_binary_) {
    IRMetalLibBinaryDestroy(metal_lib_binary_);
    metal_lib_binary_ = nullptr;
  }
  if (ir_object_) {
    IRObjectDestroy(ir_object_);
    ir_object_ = nullptr;
  }
  if (ir_compiler_) {
    IRCompilerDestroy(ir_compiler_);
    ir_compiler_ = nullptr;
  }
}

bool MetalShader::MetalTranslation::ConvertToMetal() {
  XELOGI("========== Metal shader: ConvertToMetal() CALLED ==========");
  
  // Initialize the DXBC to DXIL converter if not already done
  if (!g_dxbc_to_dxil_converter) {
    XELOGI("Metal shader: Initializing DXBC to DXIL converter...");
    g_dxbc_to_dxil_converter = std::make_unique<DxbcToDxilConverter>();
    if (!g_dxbc_to_dxil_converter->Initialize()) {
      XELOGE("Metal shader: Failed to initialize DXBC to DXIL converter");
      XELOGE("Make sure Wine is installed and dxbc2dxil.exe is available");
      return false;
    }
    XELOGI("Metal shader: DXBC to DXIL converter initialized successfully");
  }

  // Initialize shader cache if not already done
  if (!g_metal_shader_cache) {
    g_metal_shader_cache = std::make_unique<MetalShaderCache>();
    // Optionally load from disk
    // g_metal_shader_cache->LoadFromFile("shader_cache.bin");
  }

  // Get the DXBC bytecode from the base class
  // NOTE: Despite the method name, this actually returns DXBC, not DXIL
  const auto& dxbc_bytecode = translated_binary();
  if (dxbc_bytecode.empty()) {
    XELOGE("Metal shader: No DXBC bytecode available for conversion");
    return false;
  }
  
  XELOGD("Metal shader: Got DXBC bytecode, size = {} bytes", dxbc_bytecode.size());
  

  // Get shader hash for caching
  uint64_t shader_hash = shader().ucode_data_hash();
  
  // Check cache first
  std::vector<uint8_t> dxil_bytecode;
  if (!g_metal_shader_cache->GetCachedDxil(shader_hash, dxil_bytecode)) {
    // Not in cache, need to convert
    std::string error_message;
    
    XELOGD("Metal shader: Converting DXBC ({} bytes) to DXIL...", dxbc_bytecode.size());
    
    if (!g_dxbc_to_dxil_converter->Convert(dxbc_bytecode, dxil_bytecode, &error_message)) {
      XELOGE("Metal shader: DXBC to DXIL conversion failed: {}", error_message);
      return false;
    }
    
    // Store in cache for next time
    g_metal_shader_cache->StoreDxil(shader_hash, dxil_bytecode);
  } else {
    XELOGD("Metal shader: Found DXIL in cache ({} bytes)", dxil_bytecode.size());
  }

  XELOGD("Metal shader: Successfully got DXIL ({} bytes)", dxil_bytecode.size());
  

  // Convert DXIL to Metal using Metal Shader Converter
  if (!ConvertDxilToMetal(dxil_bytecode)) {
    XELOGE("Metal shader: Failed to convert DXIL to Metal");
    return false;
  }

  // Create Metal library from the converted IR
  if (!CreateMetalLibrary()) {
    XELOGE("Metal shader: Failed to create Metal library");
    return false;
  }

  XELOGD("Metal shader: Successfully converted {} shader to Metal",
         shader().type() == xenos::ShaderType::kVertex ? "vertex" : "pixel");
  return true;
}

bool MetalShader::MetalTranslation::ConvertDxilToMetal(
    const std::vector<uint8_t>& dxil_bytecode) {
  // Create IR compiler
  ir_compiler_ = IRCompilerCreate();
  if (!ir_compiler_) {
    XELOGE("Metal shader: Failed to create IRCompiler");
    return false;
  }

  // Set entry point name based on shader type
  const char* entry_point = 
      shader().type() == xenos::ShaderType::kVertex ? "main_vs" : "main_ps";
  IRCompilerSetEntryPointName(ir_compiler_, entry_point);

  // Configure Metal Shader Converter for Xbox 360 compatibility
  // Enable texture array compatibility for Metal Shader Converter 2.x compatibility
  IRCompilerSetCompatibilityFlags(ir_compiler_, 
                                  IRCompatibilityFlagForceTextureArray);

  // Set minimum deployment target for Metal features
  IRCompilerSetMinimumDeploymentTarget(ir_compiler_, IROperatingSystem_macOS, "14.0");
    

  // Create IR object from DXIL bytecode
  XELOGD("Metal shader: Creating IR object from DXIL ({} bytes)...", dxil_bytecode.size());
  
  // DEBUG: Print first few bytes of DXIL to check header
  if (dxil_bytecode.size() >= 16) {
    XELOGD("Metal shader: DXIL header bytes: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
           dxil_bytecode[0], dxil_bytecode[1], dxil_bytecode[2], dxil_bytecode[3],
           dxil_bytecode[4], dxil_bytecode[5], dxil_bytecode[6], dxil_bytecode[7]);
  }
  
  IRObject* dxil_object = IRObjectCreateFromDXIL(
      dxil_bytecode.data(), dxil_bytecode.size(), IRBytecodeOwnershipNone);
  if (!dxil_object) {
    XELOGE("Metal shader: Failed to create IR object from DXIL - IRObjectCreateFromDXIL returned nullptr");
    return false;
  }
  XELOGD("Metal shader: Successfully created IR object from DXIL");

  // Compile DXIL to Metal IR
  IRError* error = nullptr;
  ir_object_ = IRCompilerAllocCompileAndLink(ir_compiler_, nullptr, 
                                             dxil_object, &error);
  
  // Clean up the temporary DXIL object
  IRObjectDestroy(dxil_object);

  if (!ir_object_) {
    if (error) {
      uint32_t error_code = IRErrorGetCode(error);
      const char* error_payload = (const char*)IRErrorGetPayload(error);
      XELOGE("Metal shader: Compilation failed with error code: {}", error_code);
      
      // Try to extract error string if available
      if (error_payload) {
        // Error payload might contain additional information
        XELOGE("Metal shader: Failed with error payload: {}", error_payload);
        
      }
      
      IRErrorDestroy(error);
    } else {
      XELOGE("Metal shader: Compilation failed with unknown error");
    }
    return false;
  }
  
  // Capture reflection data after successful compilation
  if (!CaptureReflectionData()) {
    XELOGE("Metal shader: Failed to capture reflection data");
    // Don't fail compilation - we can still try with default offsets
  }

  return true;
}

bool MetalShader::MetalTranslation::CreateMetalLibrary() {
  if (!ir_object_) {
    XELOGE("Metal shader: No IR object available for Metal library creation");
    return false;
  }

  // Create Metal library binary
  metal_lib_binary_ = IRMetalLibBinaryCreate();
  if (!metal_lib_binary_) {
    XELOGE("Metal shader: Failed to create Metal library binary");
    return false;
  }

  // Get the appropriate shader stage
  IRShaderStage stage = shader().type() == xenos::ShaderType::kVertex 
                        ? IRShaderStageVertex : IRShaderStageFragment;

  // Extract metallib from IR object
  if (!IRObjectGetMetalLibBinary(ir_object_, stage, metal_lib_binary_)) {
    XELOGE("Metal shader: Failed to get Metal library binary");
    return false;
  }

  // Get metallib bytecode
  size_t metallib_size = IRMetalLibGetBytecodeSize(metal_lib_binary_);
  if (metallib_size == 0) {
    XELOGE("Metal shader: Metal library binary is empty");
    return false;
  }

  auto* metallib_data = new uint8_t[metallib_size];
  IRMetalLibGetBytecode(metal_lib_binary_, metallib_data);

  // Create MTLLibrary from bytecode using metal-cpp
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    XELOGE("Metal shader: Failed to get Metal device");
    delete[] metallib_data;
    return false;
  }

  NS::Error* error = nullptr;
  
  // Create dispatch data with cleanup block
  auto cleanup_block = ^{
    delete[] metallib_data;
  };
  
  dispatch_data_t dispatch_data = dispatch_data_create(
      metallib_data, metallib_size, dispatch_get_main_queue(),
      cleanup_block);
  
  MTL::Library* library = device->newLibrary(dispatch_data, &error);
  dispatch_release(dispatch_data);
  
  if (!library) {
    NS::String* error_desc = error ? error->localizedDescription() : NS::String::string("Unknown error", NS::UTF8StringEncoding);
    XELOGE("Metal shader: Failed to create MTLLibrary: {}", error_desc->utf8String());
    device->release();
    return false;
  }

  // Get the function from the library  
  const char* function_name = shader().type() == xenos::ShaderType::kVertex ? "main_vs" : "main_ps";
  NS::String* func_name_str = NS::String::string(function_name, NS::UTF8StringEncoding);
  MTL::Function* function = library->newFunction(func_name_str);
  func_name_str->release();
  
  if (!function) {
    XELOGE("Metal shader: Failed to get function '{}' from Metal library", function_name);
    library->release();
    device->release();
    return false;
  }

  // Store the library and function
  metal_library_ = library;
  metal_function_ = function;

  device->release();
  return true;
}

bool MetalShader::MetalTranslation::CaptureReflectionData() {
  if (!ir_object_) {
    XELOGE("Metal shader: No IR object for reflection");
    return false;
  }
  
  // Get shader stage
  IRShaderStage stage = shader().type() == xenos::ShaderType::kVertex 
                        ? IRShaderStageVertex : IRShaderStageFragment;
  
  // Create reflection object
  IRShaderReflection* reflection = IRShaderReflectionCreate();
  if (!IRObjectGetReflection(ir_object_, stage, reflection)) {
    XELOGE("Metal shader: Failed to get shader reflection");
    IRShaderReflectionDestroy(reflection);
    return false;
  }
  
  // Get resource count
  size_t resource_count = IRShaderReflectionGetResourceCount(reflection);
  XELOGI("Metal shader: Reflection found {} resources", resource_count);
  
  if (resource_count == 0) {
    // No resources - this is valid for some shaders
    IRShaderReflectionDestroy(reflection);
    return true;
  }
  
  // Allocate array for resource locations
  IRResourceLocation* locations = new IRResourceLocation[resource_count];
  
  // Get actual resource locations
  IRShaderReflectionGetResourceLocations(reflection, locations);
  
  // Clear existing mappings and reserve space
  resource_mappings_.clear();
  resource_mappings_.reserve(resource_count);
  
  // Store resource mappings
  for (size_t i = 0; i < resource_count; i++) {
    ResourceMapping mapping;
    mapping.hlsl_slot = locations[i].slot;
    mapping.hlsl_space = locations[i].space;
    mapping.resource_type = locations[i].resourceType;  // Use correct field name
    
    // The topLevelOffset is in bytes into the argument buffer
    // Convert to entry index (each entry is 3 uint64_t = 24 bytes)
    mapping.arg_buffer_offset = locations[i].topLevelOffset / sizeof(IRDescriptorTableEntry);
    
    resource_mappings_.push_back(mapping);
    
    // Debug logging
    const char* type_str = "Unknown";
    switch (locations[i].resourceType) {
      case IRResourceTypeTable: type_str = "Table"; break;
      case IRResourceTypeConstant: type_str = "Constant"; break;
      case IRResourceTypeCBV: type_str = "CBV"; break;
      case IRResourceTypeSRV: type_str = "SRV/Texture"; break;
      case IRResourceTypeUAV: type_str = "UAV"; break;
      case IRResourceTypeSampler: type_str = "Sampler"; break;
      case IRResourceTypeInvalid: type_str = "Invalid"; break;
      default: type_str = "Unknown"; break;
    }
    
    XELOGI("  Resource[{}]: type={} (raw={}), slot={}, space={}, offset_bytes={}, offset_entries={}",
           i, type_str, (int)locations[i].resourceType, locations[i].slot, locations[i].space, 
           locations[i].topLevelOffset, mapping.arg_buffer_offset);
  }
  
  delete[] locations;
  
  // Also capture shader stage info for debugging
  const char* stage_str = (stage == IRShaderStageVertex) ? "Vertex" : "Fragment";
  XELOGI("Metal shader: {} shader reflection captured with {} resources",
         stage_str, resource_count);
  
  // Clean up reflection object
  IRShaderReflectionDestroy(reflection);
  
  return true;
}

// Cleanup function to be called on shutdown
void CleanupMetalShaderResources() {
  if (g_metal_shader_cache) {
    // Optionally save cache to disk
    // g_metal_shader_cache->SaveToFile("shader_cache.bin");
    XELOGD("Metal shader cache stats: {} shaders, {} total bytes",
           g_metal_shader_cache->GetCacheSize(),
           g_metal_shader_cache->GetTotalBytes());
    g_metal_shader_cache.reset();
  }
  
  if (g_dxbc_to_dxil_converter) {
    g_dxbc_to_dxil_converter.reset();
  }
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
