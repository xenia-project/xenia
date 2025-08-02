/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"

#if XE_PLATFORM_MAC
// Define metal-cpp implementation (only in one cpp file)
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION  
#include "third_party/metal-cpp/Metal/Metal.hpp"
#endif

namespace xe {
namespace gpu {
namespace metal {

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
#if XE_PLATFORM_MAC
#ifdef METAL_CPP_AVAILABLE
  // Clean up Metal objects (metal-cpp uses RAII)
  if (metal_function_) {
    metal_function_->release();
    metal_function_ = nullptr;
  }
  if (metal_library_) {
    metal_library_->release();
    metal_library_ = nullptr;
  }
#endif  // METAL_CPP_AVAILABLE

#ifdef METAL_SHADER_CONVERTER_AVAILABLE
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
#endif  // METAL_SHADER_CONVERTER_AVAILABLE
#endif  // XE_PLATFORM_MAC
}

bool MetalShader::MetalTranslation::ConvertToMetal() {
  // Get the DXIL bytecode from the base class
  const auto& dxil_bytecode = translated_binary();
  if (dxil_bytecode.empty()) {
    XELOGE("Metal shader: No DXIL bytecode available for Metal conversion");
    return false;
  }

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
#if XE_PLATFORM_MAC
#ifdef METAL_SHADER_CONVERTER_AVAILABLE
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
  IRCompilerSetMinimumDeploymentTarget(ir_compiler_, IROperatingSystemmacOS, 14, 0);

  // Create IR object from DXIL bytecode
  IRObject* dxil_object = IRObjectCreateFromDXIL(
      dxil_bytecode.data(), dxil_bytecode.size(), IRBytecodeOwnershipNone);
  if (!dxil_object) {
    XELOGE("Metal shader: Failed to create IR object from DXIL");
    return false;
  }

  // Compile DXIL to Metal IR
  IRError* error = nullptr;
  ir_object_ = IRCompilerAllocCompileAndLink(ir_compiler_, nullptr, 
                                             dxil_object, &error);
  
  // Clean up the temporary DXIL object
  IRObjectDestroy(dxil_object);

  if (!ir_object_) {
    if (error) {
      // TODO: Extract error message from IRError
      XELOGE("Metal shader: Compilation failed");
      IRErrorDestroy(error);
    }
    return false;
  }

  return true;
#else
  XELOGE("Metal shader: Metal Shader Converter not available");
  return false;
#endif  // METAL_SHADER_CONVERTER_AVAILABLE
#else
  XELOGE("Metal shader: Not supported on non-macOS platforms");
  return false;
#endif  // XE_PLATFORM_MAC
}

bool MetalShader::MetalTranslation::CreateMetalLibrary() {
#if XE_PLATFORM_MAC
#if defined(METAL_SHADER_CONVERTER_AVAILABLE) && defined(METAL_CPP_AVAILABLE)
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
  dispatch_data_t data = dispatch_data_create(metallib_data, metallib_size, 
                                              dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  
  MTL::Library* library = device->newLibrary(data, &error);
  dispatch_release(data);
  
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
#else
  XELOGE("Metal shader: Metal Shader Converter or metal-cpp not available");
  return false;
#endif  // METAL_SHADER_CONVERTER_AVAILABLE && METAL_CPP_AVAILABLE
#else
  XELOGE("Metal shader: Not supported on non-macOS platforms");
  return false;
#endif  // XE_PLATFORM_MAC
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
