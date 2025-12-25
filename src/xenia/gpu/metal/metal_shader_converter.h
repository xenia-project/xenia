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

struct IRVersionedInputLayoutDescriptor;

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace metal {

// Shader stage for Metal conversion
enum class MetalShaderStage { kVertex, kFragment, kGeometry, kCompute };

// Result of shader conversion
struct MetalShaderConversionResult {
  bool success = false;
  std::vector<uint8_t> metallib_data;
  std::string error_message;
  std::string function_name;  // Main function name in the metallib
  bool has_mesh_stage = false;
  bool has_geometry_stage = false;
};

struct MetalShaderReflectionInput {
  std::string name;
  uint8_t attribute_index = 0;
};

struct MetalShaderFunctionConstant {
  std::string name;
  uint32_t type = 0;
};

struct MetalShaderReflectionInfo {
  uint32_t vertex_output_size_in_bytes = 0;
  uint32_t vertex_input_count = 0;
  std::vector<MetalShaderReflectionInput> vertex_inputs;
  uint32_t gs_max_input_primitives_per_mesh_threadgroup = 0;
  std::vector<MetalShaderFunctionConstant> function_constants;
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

  bool ConvertWithStageEx(MetalShaderStage stage,
                          const std::vector<uint8_t>& dxil_data,
                          MetalShaderConversionResult& result,
                          MetalShaderReflectionInfo* reflection,
                          const IRVersionedInputLayoutDescriptor* input_layout,
                          std::vector<uint8_t>* stage_in_metallib,
                          bool enable_geometry_emulation,
                          int input_topology);

  void SetMinimumTarget(uint32_t gpu_family, uint32_t os,
                        const std::string& version);

 private:
  bool is_available_ = false;
  bool has_minimum_target_ = false;
  uint32_t minimum_gpu_family_ = 0;
  uint32_t minimum_os_ = 0;
  std::string minimum_os_version_;

  // Create Xbox 360 root signature for the given shader visibility
  void* CreateXbox360RootSignature(MetalShaderStage stage,
                                   bool force_all_visibility);

  // Destroy a root signature
  void DestroyRootSignature(void* root_sig);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_SHADER_CONVERTER_H_
