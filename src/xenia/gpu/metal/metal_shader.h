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

#if XE_PLATFORM_MAC
#ifdef METAL_SHADER_CONVERTER_AVAILABLE
#include <metal_irconverter.h>
#include <metal_irconverter_runtime.h>
#endif  // METAL_SHADER_CONVERTER_AVAILABLE
#ifdef METAL_CPP_AVAILABLE
#include "third_party/metal-cpp/Metal/Metal.hpp"
#endif  // METAL_CPP_AVAILABLE
#endif  // XE_PLATFORM_MAC

namespace xe {
namespace gpu {
namespace metal {

class MetalShader : public DxbcShader {
 public:
  class MetalTranslation : public DxbcTranslation {
   public:
    MetalTranslation(MetalShader& shader, uint64_t modification);
    ~MetalTranslation() override;

    // Convert DXIL to Metal (called by the Metal backend when needed)
    bool ConvertToMetal();

    // Get the compiled Metal library for pipeline creation
#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
    MTL::Library* GetMetalLibrary() const { return metal_library_; }
    MTL::Function* GetMetalFunction() const { return metal_function_; }
#endif

   private:
    MetalShader& metal_shader_;

#if XE_PLATFORM_MAC
#ifdef METAL_SHADER_CONVERTER_AVAILABLE
    // Metal Shader Converter objects
    IRCompiler* ir_compiler_ = nullptr;
    IRObject* ir_object_ = nullptr;
    IRMetalLibBinary* metal_lib_binary_ = nullptr;
#endif  // METAL_SHADER_CONVERTER_AVAILABLE
    
#ifdef METAL_CPP_AVAILABLE
    // Metal library and function (using metal-cpp)
    MTL::Library* metal_library_ = nullptr;
    MTL::Function* metal_function_ = nullptr;
#endif  // METAL_CPP_AVAILABLE
#endif  // XE_PLATFORM_MAC

    // Convert DXIL bytecode to Metal IR using Metal Shader Converter
    bool ConvertDxilToMetal(const std::vector<uint8_t>& dxil_bytecode);
    
    // Create Metal library from Metal IR
    bool CreateMetalLibrary();
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

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_H_
