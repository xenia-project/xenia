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
  // Root signature must be destroyed before compiler
  if (root_signature_) {
    IRRootSignatureDestroy(root_signature_);
    root_signature_ = nullptr;
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
  // REMOVED ForceTextureArray - it requires all textures to be 2DArray which doesn't match our TextureType2D
  // Using default MSC 3.0 behavior for proper texture type matching
  // IRCompilerSetCompatibilityFlags(ir_compiler_, 
  //                                 IRCompatibilityFlagForceTextureArray);

  // Set minimum deployment target for Metal features
  IRCompilerSetMinimumDeploymentTarget(ir_compiler_, IROperatingSystem_macOS, "14.0");
  
  // Create and set root signature for consistent resource layout
  // This fixes the issue where shaders were looking for t0 in space 0
  // but the root signature was putting SRVs in space 1
  if (!CreateAndSetRootSignature()) {
    XELOGE("Metal shader: Failed to create root signature");
    // Continue without root signature - MSC will use automatic layout
  } else {
    XELOGI("Metal shader: Root signature enabled - using defined resource layout");
  }
    

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
  XELOGD("Metal shader: About to call IRCompilerAllocCompileAndLink...");
  IRError* error = nullptr;
  ir_object_ = IRCompilerAllocCompileAndLink(ir_compiler_, nullptr, 
                                             dxil_object, &error);
  XELOGD("Metal shader: IRCompilerAllocCompileAndLink returned");
  
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

  // DEBUG: Log function signature info
  XELOGI("Metal shader: Successfully loaded {} function '{}'", 
         shader().type() == xenos::ShaderType::kVertex ? "vertex" : "fragment",
         function_name);
  
  // Save metallib for debugging (can be decompiled with xcrun metal-source)
  // TODO: Add proper dump_shaders cvar
  {
    char filename[256];
    snprintf(filename, sizeof(filename), "shader_%s_%016llx.metallib",
             shader().type() == xenos::ShaderType::kVertex ? "vs" : "ps",
             (unsigned long long)shader().ucode_data_hash());
    FILE* f = fopen(filename, "wb");
    if (f) {
      fwrite(metallib_data, 1, metallib_size, f);
      fclose(f);
      XELOGI("Metal shader: Saved metallib to {} (decompile with: xcrun metal-source -o {}.metal {})",
             filename, filename, filename);
    }
  }

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
    
    // Extra detail for understanding bindings
    if (locations[i].resourceType == IRResourceTypeTable) {
      XELOGI("    -> This is a descriptor table at argument buffer index 2");
    } else if (locations[i].resourceType == IRResourceTypeConstant) {
      XELOGI("    -> Constants expected at buffer index {}", locations[i].slot);
    }
  }
  
  delete[] locations;
  
  // Also capture shader stage info for debugging
  const char* stage_str = (stage == IRShaderStageVertex) ? "Vertex" : "Fragment";
  XELOGI("Metal shader: {} shader reflection captured with {} resources",
         stage_str, resource_count);
  
  // Get the entry point function name
  const char* entry_point = IRShaderReflectionGetEntryPointFunctionName(reflection);
  XELOGI("Metal shader: Entry point function name: '{}'", entry_point ? entry_point : "null");
  
  // Dump full reflection as JSON for debugging
  const char* json = IRShaderReflectionCopyJSONString(reflection);
  if (json) {
    XELOGI("Metal shader: Full reflection JSON for {} shader (hash {:016x}):",
           stage_str, shader().ucode_data_hash());
    
    // CRITICAL: With root signature, reflection format may be different!
    XELOGI("===== START REFLECTION JSON =====");
    
    // Log the JSON line by line for readability
    std::string json_str(json);
    size_t pos = 0;
    int line_count = 0;
    while (pos < json_str.length() && line_count < 100) { // Limit to first 100 lines
      size_t end = json_str.find('\n', pos);
      if (end == std::string::npos) end = json_str.length();
      XELOGI("  {}", json_str.substr(pos, end - pos));
      pos = end + 1;
      line_count++;
    }
    
    if (line_count >= 100) {
      XELOGI("  ... (truncated, JSON too long)");
    }
    
    XELOGI("===== END REFLECTION JSON =====");
    
    // Parse JSON to extract texture bindings
    ParseTextureBindingsFromJSON(json_str);
    
    // Parse JSON to extract resource requirements for smart binding
    ParseResourceRequirementsFromJSON(json_str);
    
    IRShaderReflectionReleaseString(json);
  }
  
  // Clean up reflection object
  IRShaderReflectionDestroy(reflection);
  
  return true;
}

void MetalShader::MetalTranslation::ParseTextureBindingsFromJSON(const std::string& json_str) {
  // Parse TopLevelArgumentBuffer first to get the full layout
  size_t tlab_pos = json_str.find("\"TopLevelArgumentBuffer\":");
  if (tlab_pos != std::string::npos) {
    size_t tlab_start = json_str.find('[', tlab_pos);
    size_t tlab_end = json_str.find(']', tlab_start);
    
    if (tlab_start != std::string::npos && tlab_end != std::string::npos) {
      // Clear existing layout
      top_level_ab_layout_.clear();
      
      // Parse each entry in the array
      size_t pos = tlab_start + 1;
      while (pos < tlab_end) {
        // Find next object start
        size_t obj_start = json_str.find('{', pos);
        if (obj_start == std::string::npos || obj_start >= tlab_end) break;
        
        // Find object end
        size_t obj_end = json_str.find('}', obj_start);
        if (obj_end == std::string::npos || obj_end >= tlab_end) break;
        
        std::string obj_str = json_str.substr(obj_start, obj_end - obj_start + 1);
        
        ABEntry entry;
        
        // Parse EltOffset
        size_t offset_pos = obj_str.find("\"EltOffset\":");
        if (offset_pos != std::string::npos) {
          size_t num_start = obj_str.find(':', offset_pos) + 1;
          size_t num_end = obj_str.find_first_of(",}", num_start);
          std::string offset_str = obj_str.substr(num_start, num_end - num_start);
          try {
            entry.elt_offset = std::stoul(offset_str);
          } catch (const std::exception& e) {
            entry.elt_offset = 0;
          }
        }
        
        // Parse Name
        size_t name_pos = obj_str.find("\"Name\":");
        if (name_pos != std::string::npos) {
          size_t quote_start = obj_str.find('"', name_pos + 7);
          size_t quote_end = obj_str.find('"', quote_start + 1);
          entry.name = obj_str.substr(quote_start + 1, quote_end - quote_start - 1);
        }
        
        // Parse Slot
        size_t slot_pos = obj_str.find("\"Slot\":");
        if (slot_pos != std::string::npos) {
          size_t num_start = obj_str.find(':', slot_pos) + 1;
          size_t num_end = obj_str.find_first_of(",}", num_start);
          std::string slot_str = obj_str.substr(num_start, num_end - num_start);
          try {
            entry.slot = std::stoul(slot_str);
          } catch (const std::exception& e) {
            entry.slot = 0;
          }
        }
        
        // Parse Space
        size_t space_pos = obj_str.find("\"Space\":");
        if (space_pos != std::string::npos) {
          size_t num_start = obj_str.find(':', space_pos) + 1;
          size_t num_end = obj_str.find_first_of(",}", num_start);
          std::string space_str = obj_str.substr(num_start, num_end - num_start);
          try {
            entry.space = std::stoul(space_str);
          } catch (const std::exception& e) {
            entry.space = 0;
          }
        }
        
        // Parse Type
        size_t type_pos = obj_str.find("\"Type\":");
        if (type_pos != std::string::npos) {
          size_t quote_start = obj_str.find('"', type_pos + 7);
          size_t quote_end = obj_str.find('"', quote_start + 1);
          std::string type_str = obj_str.substr(quote_start + 1, quote_end - quote_start - 1);
          
          if (type_str == "SRV") {
            entry.kind = ABEntry::Kind::SRV;
          } else if (type_str == "UAV") {
            entry.kind = ABEntry::Kind::UAV;
          } else if (type_str == "CBV") {
            entry.kind = ABEntry::Kind::CBV;
          } else if (type_str == "Sampler") {
            entry.kind = ABEntry::Kind::Sampler;
          } else {
            entry.kind = ABEntry::Kind::Unknown;
          }
        }
        
        top_level_ab_layout_.push_back(entry);
        
        XELOGI("Metal shader: TopLevelAB entry - name: {}, offset: {}, slot: {}, space: {}, kind: {}",
               entry.name, entry.elt_offset, entry.slot, entry.space, 
               entry.kind == ABEntry::Kind::SRV ? "SRV" :
               entry.kind == ABEntry::Kind::UAV ? "UAV" :
               entry.kind == ABEntry::Kind::CBV ? "CBV" :
               entry.kind == ABEntry::Kind::Sampler ? "Sampler" : "Unknown");
        
        pos = obj_end + 1;
      }
      
      XELOGI("Metal shader: Parsed {} top-level AB entries", top_level_ab_layout_.size());
    }
  }
  
  // Simple JSON parser to extract ShaderResourceViewIndices array
  // Looking for: "ShaderResourceViewIndices":[0,1,2,...]
  
  size_t srv_pos = json_str.find("\"ShaderResourceViewIndices\":");
  if (srv_pos == std::string::npos) {
    XELOGE("Metal shader: WARNING - No ShaderResourceViewIndices in reflection!");
    XELOGE("This means NO texture slots will be available for binding!");
    XELOGE("This is likely due to root signature changing how MSC generates reflection.");
    
    // Try to find what keys ARE present
    XELOGI("Metal shader: Searching for alternative texture binding info...");
    if (json_str.find("\"Textures\":") != std::string::npos) {
      XELOGI("  Found 'Textures' key in reflection");
    }
    if (json_str.find("\"Resources\":") != std::string::npos) {
      XELOGI("  Found 'Resources' key in reflection");
    }
    if (json_str.find("\"DescriptorTables\":") != std::string::npos) {
      XELOGI("  Found 'DescriptorTables' key in reflection");
    }
    
    // WITH ROOT SIGNATURE: Textures are accessed through descriptor tables
    // We need to manually populate texture slots based on our knowledge
    // of the shader's texture bindings from DXBC analysis
    XELOGI("Metal shader: WITH ROOT SIGNATURE - using manual texture slot assignment");
    
    // For now, assume textures start at slot 0 and go sequentially
    // This matches how we populate the descriptor heap
    const auto& bindings = shader().texture_bindings();
    std::vector<uint32_t> texture_slots;  // Declare the vector properly
    for (size_t i = 0; i < bindings.size(); i++) {
      texture_slots.push_back(static_cast<uint32_t>(i));
      XELOGI("  Manual texture slot {} for binding {}", i, i);
    }
    
    // Store the manually created slots
    texture_slots_ = texture_slots;
    XELOGI("Metal shader: Created {} manual texture slots for root signature mode", texture_slots.size());
    return;
  }
  
  // Find the array start
  size_t array_start = json_str.find('[', srv_pos);
  if (array_start == std::string::npos) {
    return;
  }
  
  // Find the array end
  size_t array_end = json_str.find(']', array_start);
  if (array_end == std::string::npos) {
    return;
  }
  
  // Extract the array content
  std::string array_content = json_str.substr(array_start + 1, array_end - array_start - 1);
  
  // Parse the texture slot indices
  std::vector<uint32_t> texture_slots;
  if (!array_content.empty()) {
    size_t pos = 0;
    while (pos < array_content.length()) {
      // Skip whitespace
      while (pos < array_content.length() && std::isspace(array_content[pos])) {
        pos++;
      }
      
      if (pos >= array_content.length()) break;
      
      // Parse number
      size_t end = pos;
      while (end < array_content.length() && std::isdigit(array_content[end])) {
        end++;
      }
      
      if (end > pos) {
        uint32_t slot = std::stoul(array_content.substr(pos, end - pos));
        texture_slots.push_back(slot);
        XELOGI("Metal shader: Found texture slot {}", slot);
      }
      
      // Find next comma or end
      pos = array_content.find(',', end);
      if (pos != std::string::npos) {
        pos++; // Skip the comma
      } else {
        break;
      }
    }
  }
  
  // Similarly parse SamplerIndices
  std::vector<uint32_t> sampler_slots;
  size_t smp_pos = json_str.find("\"SamplerIndices\":");
  if (smp_pos != std::string::npos) {
    size_t smp_array_start = json_str.find('[', smp_pos);
    size_t smp_array_end = json_str.find(']', smp_array_start);
    if (smp_array_start != std::string::npos && smp_array_end != std::string::npos) {
      std::string smp_array_content = json_str.substr(smp_array_start + 1, 
                                                      smp_array_end - smp_array_start - 1);
      // Parse sampler indices similarly
      size_t pos = 0;
      while (pos < smp_array_content.length()) {
        while (pos < smp_array_content.length() && std::isspace(smp_array_content[pos])) {
          pos++;
        }
        if (pos >= smp_array_content.length()) break;
        
        size_t end = pos;
        while (end < smp_array_content.length() && std::isdigit(smp_array_content[end])) {
          end++;
        }
        
        if (end > pos) {
          uint32_t slot = std::stoul(smp_array_content.substr(pos, end - pos));
          sampler_slots.push_back(slot);
          XELOGI("Metal shader: Found sampler slot {}", slot);
        }
        
        pos = smp_array_content.find(',', end);
        if (pos != std::string::npos) {
          pos++;
        } else {
          break;
        }
      }
    }
  }
  
  // Store the found texture/sampler slots for later use
  // We'll use these to know which textures to bind
  texture_slots_ = texture_slots;
  sampler_slots_ = sampler_slots;
  
  // Log summary
  XELOGI("Metal shader: Parsed {} texture bindings and {} sampler bindings from reflection",
         texture_slots.size(), sampler_slots.size());
}

bool MetalShader::MetalTranslation::CreateAndSetRootSignature() {
  XELOGI("Metal shader: Root signature enabled - using defined resource layout");
  
  // ORIGINAL CODE:
  // Create descriptor ranges for resources
  // We need:
  // - CBVs (b0-b3) in register space 0
  // - SRVs (t0-t31) in register space 0 (matching DXBC expectations)
  //   - t0: Special shared memory/EDRAM texture (read)
  //   - t1-t31: Regular game textures
  // - UAVs (u0) in register space 0
  //   - u0: Shared memory/EDRAM (read-write)
  // - Samplers (s0-s31) in register space 0
  
  // Define descriptor ranges
  std::vector<IRDescriptorRange1> srv_ranges;
  std::vector<IRDescriptorRange1> uav_ranges;
  std::vector<IRDescriptorRange1> cbv_ranges;
  std::vector<IRDescriptorRange1> sampler_ranges;
  
  // CRITICAL FIX: Use reasonable descriptor counts that won't cause MSC to hang
  // Xbox 360 typically uses up to 32 textures, but we'll use 64 for safety
  
  // SRV range for all textures and shared memory (t0-t63 in space 0)
  // This should be enough for Xbox 360 games while avoiding MSC hangs
  IRDescriptorRange1 srv_range_all = {};
  srv_range_all.RangeType = IRDescriptorRangeTypeSRV;
  srv_range_all.NumDescriptors = 64;  // Reasonable count that won't hang MSC
  srv_range_all.BaseShaderRegister = 0;  // t0-t63
  srv_range_all.RegisterSpace = 0;  // Space 0 for all SRVs
  srv_range_all.OffsetInDescriptorsFromTableStart = 0;
  srv_range_all.Flags = IRDescriptorRangeFlagNone;
  srv_ranges.push_back(srv_range_all);
  
  // UAV range for shared memory write access (u0-u1 in space 0)
  // Xbox 360 can write to shared memory/EDRAM
  // Using 2 slots as recommended in Phase 22
  IRDescriptorRange1 uav_range = {};
  uav_range.RangeType = IRDescriptorRangeTypeUAV;
  uav_range.NumDescriptors = 2;  // u0-u1 for shared memory writes
  uav_range.BaseShaderRegister = 0;  // u0
  uav_range.RegisterSpace = 0;  // Space 0 for shared memory
  uav_range.OffsetInDescriptorsFromTableStart = 0;
  uav_range.Flags = IRDescriptorRangeFlagNone;
  uav_ranges.push_back(uav_range);
  
  // CBV ranges for constant buffers (b0-b3 in space 0)
  for (uint32_t i = 0; i < 4; i++) {
    IRDescriptorRange1 cbv_range = {};
    cbv_range.RangeType = IRDescriptorRangeTypeCBV;
    cbv_range.NumDescriptors = 1;
    cbv_range.BaseShaderRegister = i;  // b0, b1, b2, b3
    cbv_range.RegisterSpace = 0;  // Default space
    cbv_range.OffsetInDescriptorsFromTableStart = i;
    cbv_range.Flags = IRDescriptorRangeFlagNone;
    cbv_ranges.push_back(cbv_range);
  }
  
  // Sampler range (s0-s31 in space 0)
  // Xbox 360 supports up to 32 samplers per shader
  IRDescriptorRange1 sampler_range = {};
  sampler_range.RangeType = IRDescriptorRangeTypeSampler;
  sampler_range.NumDescriptors = 32;  // Xbox 360 max
  sampler_range.BaseShaderRegister = 0;  // s0-s31
  sampler_range.RegisterSpace = 0;
  sampler_range.OffsetInDescriptorsFromTableStart = 0;
  sampler_range.Flags = IRDescriptorRangeFlagNone;
  sampler_ranges.push_back(sampler_range);
  
  // Create root parameters
  std::vector<IRRootParameter1> root_parameters;
  
  // Parameter 0-3: Individual CBVs (b0-b3)
  for (uint32_t i = 0; i < 4; i++) {
    IRRootParameter1 cbv_param = {};
    cbv_param.ParameterType = IRRootParameterTypeCBV;
    cbv_param.Descriptor.ShaderRegister = i;  // b0, b1, b2, b3
    cbv_param.Descriptor.RegisterSpace = 0;
    cbv_param.ShaderVisibility = IRShaderVisibilityAll;
    root_parameters.push_back(cbv_param);
  }
  
  // Parameter 4: Descriptor table for SRVs (textures)
  IRRootParameter1 srv_table = {};
  srv_table.ParameterType = IRRootParameterTypeDescriptorTable;
  srv_table.DescriptorTable.NumDescriptorRanges = srv_ranges.size();
  srv_table.DescriptorTable.pDescriptorRanges = srv_ranges.data();
  srv_table.ShaderVisibility = IRShaderVisibilityAll;
  root_parameters.push_back(srv_table);
  
  // Parameter 5: Descriptor table for UAVs (shared memory write)
  IRRootParameter1 uav_table = {};
  uav_table.ParameterType = IRRootParameterTypeDescriptorTable;
  uav_table.DescriptorTable.NumDescriptorRanges = uav_ranges.size();
  uav_table.DescriptorTable.pDescriptorRanges = uav_ranges.data();
  uav_table.ShaderVisibility = IRShaderVisibilityAll;
  root_parameters.push_back(uav_table);
  
  // Parameter 6: Descriptor table for samplers
  IRRootParameter1 sampler_table = {};
  sampler_table.ParameterType = IRRootParameterTypeDescriptorTable;
  sampler_table.DescriptorTable.NumDescriptorRanges = sampler_ranges.size();
  sampler_table.DescriptorTable.pDescriptorRanges = sampler_ranges.data();
  sampler_table.ShaderVisibility = IRShaderVisibilityAll;
  root_parameters.push_back(sampler_table);
  
  // Create root signature descriptor
  IRRootSignatureDescriptor1 root_desc = {};
  root_desc.NumParameters = root_parameters.size();
  root_desc.pParameters = root_parameters.data();
  root_desc.NumStaticSamplers = 0;
  root_desc.pStaticSamplers = nullptr;
  root_desc.Flags = IRRootSignatureFlagNone;
  
  // Create versioned descriptor
  IRVersionedRootSignatureDescriptor versioned_desc = {};
  versioned_desc.version = IRRootSignatureVersion_1_1;
  versioned_desc.desc_1_1 = root_desc;
  
  // Create root signature
  IRError* error = nullptr;
  root_signature_ = IRRootSignatureCreateFromDescriptor(&versioned_desc, &error);
  
  if (!root_signature_) {
    if (error) {
      uint32_t error_code = IRErrorGetCode(error);
      const char* error_msg = (const char*)IRErrorGetPayload(error);
      XELOGE("Metal shader: Failed to create root signature. Error code: {}, Message: {}",
             error_code, error_msg ? error_msg : "Unknown");
      IRErrorDestroy(error);
    } else {
      XELOGE("Metal shader: Failed to create root signature (unknown error)");
    }
    return false;
  }
  
  // Set the root signature on the compiler
  // NOTE: The compiler does NOT make a copy - we must keep root_signature_ alive!
  IRCompilerSetGlobalRootSignature(ir_compiler_, root_signature_);
  
  XELOGI("Metal shader: Successfully created and set root signature with:");
  XELOGI("  - 4 CBVs (b0-b3) in space 0");
  XELOGI("  - 64 SRVs (t0-t63) in space 0");
  XELOGI("    - t0: Shared memory/EDRAM texture (read)");
  XELOGI("    - t1-t63: Regular game textures");
  XELOGI("  - 2 UAVs (u0-u1) in space 0");
  XELOGI("    - u0-u1: Shared memory/EDRAM (read-write)");
  XELOGI("  - 32 Samplers (s0-s31) in space 0");
  
  // DO NOT destroy the root signature here - it must remain valid until compilation is done!
  // It will be cleaned up in the destructor
  
  return true;
}

void MetalShader::MetalTranslation::ParseResourceRequirementsFromJSON(const std::string& json_str) {
  // Reset requirements to defaults
  resource_requirements_ = ShaderResourceRequirements();
  
  // Parse "UsedResources" array to determine what the shader actually uses
  // This is critical for avoiding Metal validation errors about unused bindings
  size_t used_res_pos = json_str.find("\"UsedResources\":");
  if (used_res_pos != std::string::npos) {
    size_t array_start = json_str.find('[', used_res_pos);
    size_t array_end = json_str.find(']', array_start);
    
    if (array_start != std::string::npos && array_end != std::string::npos) {
      std::string array_content = json_str.substr(array_start + 1, array_end - array_start - 1);
      
      // Parse each resource object in the array
      size_t pos = 0;
      while (pos < array_content.length()) {
        // Find next object
        size_t obj_start = array_content.find('{', pos);
        if (obj_start == std::string::npos) break;
        
        size_t obj_end = array_content.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        
        std::string obj_str = array_content.substr(obj_start, obj_end - obj_start + 1);
        
        // Parse bindingIndex to determine which buffer slot is used
        size_t binding_pos = obj_str.find("\"bindingIndex\":");
        if (binding_pos != std::string::npos) {
          size_t num_start = obj_str.find(':', binding_pos) + 1;
          size_t num_end = obj_str.find_first_of(",}", num_start);
          // Trim whitespace and safely parse the integer
          std::string num_str = obj_str.substr(num_start, num_end - num_start);
          // Trim leading/trailing whitespace
          size_t first = num_str.find_first_not_of(" \t\n\r");
          size_t last = num_str.find_last_not_of(" \t\n\r");
          if (first != std::string::npos && last != std::string::npos) {
            num_str = num_str.substr(first, last - first + 1);
          }
          int binding_index = 0;
          try {
            binding_index = std::stoi(num_str);
          } catch (const std::exception& e) {
            XELOGW("Failed to parse bindingIndex: {}", num_str);
            continue;
          }
          
          // Map binding index to buffer slot requirements
          // Based on MSC runtime buffer layout:
          switch (binding_index) {
            case 0:  // kIRDescriptorHeapBindPoint
              resource_requirements_.uses_descriptor_heap = true;
              XELOGI("Shader uses descriptor heap (slot 0)");
              break;
            case 1:  // kIRSamplerHeapBindPoint
              resource_requirements_.uses_sampler_heap = true;
              XELOGI("Shader uses sampler heap (slot 1)");
              break;
            case 2:  // kIRArgumentBufferBindPoint
              resource_requirements_.uses_top_level_ab = true;
              XELOGI("Shader uses top-level argument buffer (slot 2)");
              break;
            case 3:  // kIRArgumentBufferHullDomainBindPoint
              resource_requirements_.uses_hull_domain = true;
              XELOGI("Shader uses hull/domain (slot 3) - unexpected for Xbox 360");
              break;
            case 4:  // kIRArgumentBufferDrawArgumentsBindPoint
              resource_requirements_.uses_draw_arguments = true;
              XELOGI("Shader uses draw arguments (slot 4)");
              break;
            case 5:  // kIRArgumentBufferUniformsBindPoint
              resource_requirements_.uses_uniforms_direct = true;
              XELOGI("Shader uses uniforms direct access (slot 5)");
              break;
            default:
              if (binding_index >= 6 && binding_index <= 13) {
                resource_requirements_.uses_vertex_buffers[binding_index - 6] = true;
                XELOGI("Shader uses vertex buffer at slot {}", binding_index);
              }
              break;
          }
        }
        
        pos = obj_end + 1;
      }
    }
  } else {
    XELOGW("No UsedResources found in reflection - defaulting to minimal requirements");
    // If no UsedResources info, only enable what we know is commonly needed
    // The top-level AB is usually needed for constant buffers
    resource_requirements_.uses_top_level_ab = true;
    // Don't assume descriptor/sampler heaps are needed unless we find evidence
    resource_requirements_.uses_descriptor_heap = false;
    resource_requirements_.uses_sampler_heap = false;
  }
  
  // Check for "needs_draw_params" in state info
  if (json_str.find("\"needs_draw_params\":true") != std::string::npos ||
      json_str.find("\"needs_draw_params\": true") != std::string::npos) {
    resource_requirements_.uses_draw_arguments = true;
    XELOGI("Shader needs draw params (from state)");
  }
  
  // Parse top-level AB entries to determine which resources are accessed
  // This helps understand if CBVs are accessed directly or through top-level AB
  for (const auto& entry : top_level_ab_layout_) {
    switch (entry.kind) {
      case ABEntry::Kind::CBV:
        resource_requirements_.used_cbv_indices.push_back(entry.slot);
        // If we have CBVs in top-level AB, shader might access them directly too
        if (!resource_requirements_.used_cbv_indices.empty()) {
          // Some shaders access CBVs both ways for performance
          resource_requirements_.uses_uniforms_direct = true;
        }
        break;
      case ABEntry::Kind::SRV:
        resource_requirements_.used_srv_indices.push_back(entry.slot);
        // If we have SRVs, we need the descriptor heap
        resource_requirements_.uses_descriptor_heap = true;
        break;
      case ABEntry::Kind::UAV:
        resource_requirements_.used_uav_indices.push_back(entry.slot);
        // UAVs also use the descriptor heap
        resource_requirements_.uses_descriptor_heap = true;
        break;
      case ABEntry::Kind::Sampler:
        resource_requirements_.used_sampler_indices.push_back(entry.slot);
        // If we have samplers, we need the sampler heap
        resource_requirements_.uses_sampler_heap = true;
        break;
      default:
        break;
    }
  }
  
  // Special handling for vertex shaders - they often need uniforms for transformation
  if (shader().type() == xenos::ShaderType::kVertex) {
    // Vertex shaders typically need system constants for NDC transformation
    if (!resource_requirements_.used_cbv_indices.empty() || 
        resource_requirements_.uses_top_level_ab) {
      resource_requirements_.uses_uniforms_direct = true;
      XELOGI("Vertex shader - enabling direct uniforms access for NDC transform");
    }
  }
  
  // Log summary of resource requirements
  XELOGI("Shader resource requirements summary:");
  XELOGI("  Descriptor heap (slot 0): {}", resource_requirements_.uses_descriptor_heap);
  XELOGI("  Sampler heap (slot 1): {}", resource_requirements_.uses_sampler_heap);
  XELOGI("  Top-level AB (slot 2): {}", resource_requirements_.uses_top_level_ab);
  XELOGI("  Draw arguments (slot 4): {}", resource_requirements_.uses_draw_arguments);
  XELOGI("  Uniforms direct (slot 5): {}", resource_requirements_.uses_uniforms_direct);
  XELOGI("  CBV count: {}, SRV count: {}, Sampler count: {}",
         resource_requirements_.used_cbv_indices.size(),
         resource_requirements_.used_srv_indices.size(), 
         resource_requirements_.used_sampler_indices.size());
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
