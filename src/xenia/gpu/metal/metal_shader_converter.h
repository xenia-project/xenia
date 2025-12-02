/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_SHADER_CONVERTER_H_
#define XENIA_GPU_METAL_METAL_SHADER_CONVERTER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace metal {

// Shader stage for Metal conversion
enum class MetalShaderStage { kVertex, kFragment, kCompute };

// Result of shader conversion
struct MetalShaderConversionResult {
  bool success = false;
  std::vector<uint8_t> metallib_data;
  std::string error_message;
  std::string function_name;  // Main function name in the metallib
};

// Converts DXIL shaders to Metal IR using Apple's Metal Shader Converter
// Uses the correct Xbox 360 root signatures from xbox360_rootsig_helper.h
class MetalShaderConverter {
 public:
  MetalShaderConverter();
  ~MetalShaderConverter();

  // Initialize the converter (loads MSC library)
  bool Initialize();

  // Check if the converter is available
  bool IsAvailable() const { return is_available_; }

  // Convert DXIL to Metal IR
  // shader_type: Xenia shader type (vertex or pixel)
  // dxil_data: DXIL bytecode from dxbc2dxil
  // result: Output conversion result with metallib data
  bool Convert(xenos::ShaderType shader_type,
               const std::vector<uint8_t>& dxil_data,
               MetalShaderConversionResult& result);

  // Convert with explicit stage specification
  bool ConvertWithStage(MetalShaderStage stage,
                        const std::vector<uint8_t>& dxil_data,
                        MetalShaderConversionResult& result);

 private:
  bool is_available_ = false;

  // Create Xbox 360 root signature for the given shader visibility
  void* CreateXbox360RootSignature(MetalShaderStage stage);

  // Destroy a root signature
  void DestroyRootSignature(void* root_sig);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_CONVERTER_H_
