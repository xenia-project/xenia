/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader_converter.h"

#include "metal_irconverter.h"

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace metal {

MetalShaderConverter::MetalShaderConverter() = default;

MetalShaderConverter::~MetalShaderConverter() = default;

void MetalShaderConverter::SetMinimumTarget(uint32_t gpu_family, uint32_t os,
                                             const std::string& version) {
  has_minimum_target_ = true;
  minimum_gpu_family_ = gpu_family;
  minimum_os_ = os;
  minimum_os_version_ = version;
}

bool MetalShaderConverter::Initialize() {
  // Metal Shader Converter is a library that should be available
  // at /usr/local/lib/libmetalirconverter.dylib
  // The headers are at /usr/local/include/metal_irconverter/
  // or in third_party/metal-shader-converter/include/

  // Test if we can create basic MSC objects
  IRCompiler* test_compiler = IRCompilerCreate();
  if (!test_compiler) {
    XELOGE(
        "MetalShaderConverter: Failed to create IR compiler - MSC not "
        "available");
    is_available_ = false;
    return false;
  }
  IRCompilerDestroy(test_compiler);

  XELOGI("MetalShaderConverter: Initialized successfully");
  is_available_ = true;
  return true;
}

// Create Xbox 360 root signature matching xbox360_rootsig_helper.h
void* MetalShaderConverter::CreateXbox360RootSignature(
    MetalShaderStage stage, bool force_all_visibility) {
  IRShaderVisibility visibility = IRShaderVisibilityAll;
  if (!force_all_visibility) {
    switch (stage) {
      case MetalShaderStage::kVertex:
        visibility = IRShaderVisibilityVertex;
        break;
      case MetalShaderStage::kFragment:
        visibility = IRShaderVisibilityPixel;
        break;
      case MetalShaderStage::kCompute:
      case MetalShaderStage::kGeometry:
      default:
        visibility = IRShaderVisibilityAll;
        break;
    }
  }

  // Create descriptor ranges for Xbox 360 shader resources
  // This matches the layout in xbox360_rootsig_helper.h
  static IRDescriptorRange1 ranges[20];
  int rangeIdx = 0;

  // SRVs in spaces 0-3
  // Use 1025 descriptors (1024 + 1 padding) to match heap allocation
  for (int space = 0; space < 4; space++) {
    ranges[rangeIdx].RangeType = IRDescriptorRangeTypeSRV;
    ranges[rangeIdx].NumDescriptors =
        1025;  // Match kResourceHeapSlots (1024 + 1)
    ranges[rangeIdx].BaseShaderRegister = 0;
    ranges[rangeIdx].RegisterSpace = space;
    ranges[rangeIdx].Flags = IRDescriptorRangeFlagNone;
    ranges[rangeIdx].OffsetInDescriptorsFromTableStart =
        IRDescriptorRangeOffsetAppend;
    rangeIdx++;
  }

  // SRV in space 10 for hull shaders
  ranges[rangeIdx].RangeType = IRDescriptorRangeTypeSRV;
  ranges[rangeIdx].NumDescriptors =
      1025;  // Match kResourceHeapSlots (1024 + 1)
  ranges[rangeIdx].BaseShaderRegister = 0;
  ranges[rangeIdx].RegisterSpace = 10;
  ranges[rangeIdx].Flags = IRDescriptorRangeFlagNone;
  ranges[rangeIdx].OffsetInDescriptorsFromTableStart =
      IRDescriptorRangeOffsetAppend;
  rangeIdx++;

  // UAVs in spaces 0-3
  // Use 1025 descriptors (1024 + 1 padding) to match heap allocation
  for (int space = 0; space < 4; space++) {
    ranges[rangeIdx].RangeType = IRDescriptorRangeTypeUAV;
    ranges[rangeIdx].NumDescriptors =
        1025;  // Match kResourceHeapSlots (1024 + 1)
    ranges[rangeIdx].BaseShaderRegister = 0;
    ranges[rangeIdx].RegisterSpace = space;
    ranges[rangeIdx].Flags = IRDescriptorRangeFlagNone;
    ranges[rangeIdx].OffsetInDescriptorsFromTableStart =
        IRDescriptorRangeOffsetAppend;
    rangeIdx++;
  }

  // Samplers in space 0
  // Use 257 descriptors (256 + 1 padding) to match heap allocation
  ranges[rangeIdx].RangeType = IRDescriptorRangeTypeSampler;
  ranges[rangeIdx].NumDescriptors = 257;  // Match kSamplerHeapSlots (256 + 1)
  ranges[rangeIdx].BaseShaderRegister = 0;
  ranges[rangeIdx].RegisterSpace = 0;
  ranges[rangeIdx].Flags = IRDescriptorRangeFlagNone;
  ranges[rangeIdx].OffsetInDescriptorsFromTableStart =
      IRDescriptorRangeOffsetAppend;
  rangeIdx++;

  // CBVs in spaces 0-3
  // Xenia uses 5 CBVs (b0-b4) in space 0:
  //   b0 = system constants
  //   b1 = float constants
  //   b2 = bool/loop constants
  //   b3 = fetch constants
  //   b4 = descriptor indices (bindless)
  // We limit to 5 descriptors to match our heap allocation
  for (int space = 0; space < 4; space++) {
    ranges[rangeIdx].RangeType = IRDescriptorRangeTypeCBV;
    ranges[rangeIdx].NumDescriptors =
        (space == 0) ? 5 : 1;  // Only space 0 has multiple CBVs
    ranges[rangeIdx].BaseShaderRegister = 0;
    ranges[rangeIdx].RegisterSpace = space;
    ranges[rangeIdx].Flags = IRDescriptorRangeFlagNone;
    ranges[rangeIdx].OffsetInDescriptorsFromTableStart =
        IRDescriptorRangeOffsetAppend;
    rangeIdx++;
  }

  // Create descriptor tables and parameters
  static IRRootDescriptorTable1 tables[20];
  static IRRootParameter1 params[20];

  for (int i = 0; i < rangeIdx; i++) {
    tables[i].NumDescriptorRanges = 1;
    tables[i].pDescriptorRanges = &ranges[i];
    params[i].ParameterType = IRRootParameterTypeDescriptorTable;
    params[i].DescriptorTable = tables[i];
    params[i].ShaderVisibility = visibility;
  }

  // Create root signature descriptor
  IRRootSignatureDescriptor1 desc = {};
  desc.NumParameters = rangeIdx;
  desc.pParameters = params;
  desc.NumStaticSamplers = 0;
  desc.pStaticSamplers = nullptr;
  desc.Flags = IRRootSignatureFlagNone;

  IRVersionedRootSignatureDescriptor versionedDesc = {};
  versionedDesc.version = IRRootSignatureVersion_1_1;
  versionedDesc.desc_1_1 = desc;

  // Create the root signature
  IRError* error = nullptr;
  IRRootSignature* rootSig =
      IRRootSignatureCreateFromDescriptor(&versionedDesc, &error);

  if (error) {
    const char* errMsg = (const char*)IRErrorGetPayload(error);
    XELOGE("MetalShaderConverter: Failed to create root signature: {}",
           errMsg ? errMsg : "unknown error");
    IRErrorDestroy(error);
    return nullptr;
  }

  return rootSig;
}

void MetalShaderConverter::DestroyRootSignature(void* root_sig) {
  if (root_sig) {
    IRRootSignatureDestroy(static_cast<IRRootSignature*>(root_sig));
  }
}

bool MetalShaderConverter::Convert(xenos::ShaderType shader_type,
                                   const std::vector<uint8_t>& dxil_data,
                                   MetalShaderConversionResult& result) {
  MetalShaderStage stage;
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      stage = MetalShaderStage::kVertex;
      break;
    case xenos::ShaderType::kPixel:
      stage = MetalShaderStage::kFragment;
      break;
    default:
      result.success = false;
      result.error_message = "Unsupported shader type";
      return false;
  }

  return ConvertWithStage(stage, dxil_data, result);
}

bool MetalShaderConverter::ConvertWithStage(
    MetalShaderStage stage, const std::vector<uint8_t>& dxil_data,
    MetalShaderConversionResult& result) {
  return ConvertWithStageEx(stage, dxil_data, result, nullptr, nullptr, nullptr,
                            false, IRInputTopologyUndefined);
}

bool MetalShaderConverter::ConvertWithStageEx(
    MetalShaderStage stage, const std::vector<uint8_t>& dxil_data,
    MetalShaderConversionResult& result, MetalShaderReflectionInfo* reflection,
    const IRVersionedInputLayoutDescriptor* input_layout,
    std::vector<uint8_t>* stage_in_metallib, bool enable_geometry_emulation,
    int input_topology) {
  if (!is_available_) {
    result.success = false;
    result.error_message = "MetalShaderConverter not initialized";
    return false;
  }

  if (dxil_data.empty()) {
    result.success = false;
    result.error_message = "Empty DXIL data";
    return false;
  }

  // Create DXIL object from input data
  IRObject* dxilObject = IRObjectCreateFromDXIL(
      dxil_data.data(), dxil_data.size(), IRBytecodeOwnershipNone);

  if (!dxilObject) {
    result.success = false;
    result.error_message = "Failed to create DXIL object";
    return false;
  }

  // Create compiler
  IRCompiler* compiler = IRCompilerCreate();
  if (!compiler) {
    IRObjectDestroy(dxilObject);
    result.success = false;
    result.error_message = "Failed to create IR compiler";
    return false;
  }

  // Set compatibility flag to force texture array types
  // This is required because:
  // 1. Xenia's DXBC translator generates code expecting texture2d_array
  // 2. MSC 3.0+ defaults to non-array texture types
  // 3. Our Metal textures are created as MTLTextureType2DArray
  IRCompilerSetCompatibilityFlags(
      compiler,
      static_cast<IRCompatibilityFlags>(IRCompatibilityFlagForceTextureArray |
                                        IRCompatibilityFlagBoundsCheck));

  if (input_topology != IRInputTopologyUndefined) {
    IRCompilerSetInputTopology(
        compiler, static_cast<IRInputTopology>(input_topology));
  }
  if (enable_geometry_emulation) {
    IRCompilerEnableGeometryAndTessellationEmulation(compiler, true);
  }
  if (has_minimum_target_) {
    IRCompilerSetMinimumGPUFamily(
        compiler, static_cast<IRGPUFamily>(minimum_gpu_family_));
    IRCompilerSetMinimumDeploymentTarget(
        compiler, static_cast<IROperatingSystem>(minimum_os_),
        minimum_os_version_.c_str());
  }

  // Create and set Xbox 360 root signature
  IRRootSignature* rootSig = static_cast<IRRootSignature*>(
      CreateXbox360RootSignature(stage, enable_geometry_emulation));
  if (!rootSig) {
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxilObject);
    result.success = false;
    result.error_message = "Failed to create root signature";
    return false;
  }
  IRCompilerSetGlobalRootSignature(compiler, rootSig);

  // Compile DXIL to Metal
  IRError* error = nullptr;
  IRObject* metalObject =
      IRCompilerAllocCompileAndLink(compiler, nullptr, dxilObject, &error);

  if (error) {
    const char* errMsg = (const char*)IRErrorGetPayload(error);
    result.success = false;
    result.error_message = std::string("MSC compilation failed: ") +
                           (errMsg ? errMsg : "unknown error");
    XELOGE("MetalShaderConverter: {}", result.error_message);
    IRErrorDestroy(error);
    IRRootSignatureDestroy(rootSig);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxilObject);
    return false;
  }

  if (!metalObject) {
    result.success = false;
    result.error_message = "MSC returned null object without error";
    IRRootSignatureDestroy(rootSig);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxilObject);
    return false;
  }

  auto extract_metallib = [&](IRShaderStage ir_stage,
                              std::vector<uint8_t>& out_bytes) -> bool {
    IRMetalLibBinary* metallib = IRMetalLibBinaryCreate();
    if (!metallib) {
      return false;
    }
    bool ok = IRObjectGetMetalLibBinary(metalObject, ir_stage, metallib);
    size_t metallib_size = IRMetalLibGetBytecodeSize(metallib);
    if (!ok || metallib_size == 0) {
      IRMetalLibBinaryDestroy(metallib);
      return false;
    }
    out_bytes.resize(metallib_size);
    IRMetalLibGetBytecode(metallib, out_bytes.data());
    IRMetalLibBinaryDestroy(metallib);
    return true;
  };

  IRShaderStage ir_stage = IRShaderStageInvalid;
  switch (stage) {
    case MetalShaderStage::kVertex:
      ir_stage = IRShaderStageVertex;
      break;
    case MetalShaderStage::kFragment:
      ir_stage = IRShaderStageFragment;
      break;
    case MetalShaderStage::kCompute:
      ir_stage = IRShaderStageCompute;
      break;
    case MetalShaderStage::kGeometry:
      // We'll determine mesh/geometry below.
      break;
    default:
      ir_stage = IRShaderStageInvalid;
      break;
  }

  result.has_mesh_stage = false;
  result.has_geometry_stage = false;
  if (stage == MetalShaderStage::kGeometry) {
    std::vector<uint8_t> mesh_bytes;
    std::vector<uint8_t> geom_bytes;
    result.has_mesh_stage =
        extract_metallib(IRShaderStageMesh, mesh_bytes);
    result.has_geometry_stage =
        extract_metallib(IRShaderStageGeometry, geom_bytes);
    if (result.has_mesh_stage) {
      result.metallib_data = std::move(mesh_bytes);
      ir_stage = IRShaderStageMesh;
    } else if (result.has_geometry_stage) {
      result.metallib_data = std::move(geom_bytes);
      ir_stage = IRShaderStageGeometry;
    }
  } else if (ir_stage != IRShaderStageInvalid) {
    extract_metallib(ir_stage, result.metallib_data);
  }

  if (result.metallib_data.empty()) {
    result.success = false;
    result.error_message = "Generated MetalLib has zero size";
    IRObjectDestroy(metalObject);
    IRRootSignatureDestroy(rootSig);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxilObject);
    return false;
  }

  if (reflection) {
    reflection->vertex_inputs.clear();
    reflection->function_constants.clear();
    reflection->vertex_output_size_in_bytes = 0;
    reflection->vertex_input_count = 0;
    reflection->gs_max_input_primitives_per_mesh_threadgroup = 0;
  }

  IRShaderReflection* shader_reflection = IRShaderReflectionCreate();
  if (shader_reflection && ir_stage != IRShaderStageInvalid) {
    if (IRObjectGetReflection(metalObject, ir_stage, shader_reflection)) {
      const char* entry_name =
          IRShaderReflectionGetEntryPointFunctionName(shader_reflection);
      if (entry_name) {
        result.function_name = entry_name;
      }
      if (reflection) {
        if (ir_stage == IRShaderStageVertex) {
          IRVersionedVSInfo vs_info = {};
          vs_info.version = IRReflectionVersion_1_0;
          if (IRShaderReflectionCopyVertexInfo(shader_reflection,
                                               IRReflectionVersion_1_0,
                                               &vs_info)) {
            reflection->vertex_output_size_in_bytes =
                vs_info.info_1_0.vertex_output_size_in_bytes;
            reflection->vertex_input_count =
                static_cast<uint32_t>(vs_info.info_1_0.num_vertex_inputs);
            reflection->vertex_inputs.reserve(
                vs_info.info_1_0.num_vertex_inputs);
            for (size_t i = 0; i < vs_info.info_1_0.num_vertex_inputs; ++i) {
              const auto& input = vs_info.info_1_0.vertex_inputs[i];
              MetalShaderReflectionInput out;
              out.name = input.name ? input.name : "";
              out.attribute_index = input.attributeIndex;
              reflection->vertex_inputs.push_back(std::move(out));
            }
            IRShaderReflectionReleaseVertexInfo(&vs_info);
          }
        } else if (ir_stage == IRShaderStageGeometry ||
                   ir_stage == IRShaderStageMesh) {
          IRVersionedGSInfo gs_info = {};
          gs_info.version = IRReflectionVersion_1_0;
          if (IRShaderReflectionCopyGeometryInfo(shader_reflection,
                                                 IRReflectionVersion_1_0,
                                                 &gs_info)) {
            reflection->gs_max_input_primitives_per_mesh_threadgroup =
                gs_info.info_1_0.max_input_primitives_per_mesh_threadgroup;
            IRShaderReflectionReleaseGeometryInfo(&gs_info);
          }
        }

        if (IRShaderReflectionNeedsFunctionConstants(shader_reflection)) {
          size_t constant_count =
              IRShaderReflectionGetFunctionConstantCount(shader_reflection);
          if (constant_count) {
            std::vector<IRFunctionConstant> constants(constant_count);
            IRShaderReflectionCopyFunctionConstants(shader_reflection,
                                                    constants.data());
            reflection->function_constants.reserve(constant_count);
            for (const auto& constant : constants) {
              MetalShaderFunctionConstant out;
              out.name = constant.name ? constant.name : "";
              out.type = static_cast<uint32_t>(constant.type);
              reflection->function_constants.push_back(std::move(out));
            }
            IRShaderReflectionReleaseFunctionConstants(constants.data(),
                                                       constant_count);
          }
        }
      }
    }
  }

  if (result.function_name.empty()) {
    switch (stage) {
      case MetalShaderStage::kVertex:
        result.function_name = "vertexMain";
        break;
      case MetalShaderStage::kFragment:
        result.function_name = "fragmentMain";
        break;
      case MetalShaderStage::kCompute:
        result.function_name = "computeMain";
        break;
      case MetalShaderStage::kGeometry:
      default:
        result.function_name = "main";
        break;
    }
  }

  if (stage == MetalShaderStage::kVertex && stage_in_metallib &&
      input_layout && shader_reflection) {
    IRMetalLibBinary* stage_in_lib = IRMetalLibBinaryCreate();
    if (stage_in_lib) {
      if (IRMetalLibSynthesizeStageInFunction(
              compiler, shader_reflection, input_layout, stage_in_lib)) {
        size_t stage_in_size = IRMetalLibGetBytecodeSize(stage_in_lib);
        if (stage_in_size) {
          stage_in_metallib->resize(stage_in_size);
          IRMetalLibGetBytecode(stage_in_lib, stage_in_metallib->data());
        }
      }
      IRMetalLibBinaryDestroy(stage_in_lib);
    }
  }

  if (shader_reflection) {
    IRShaderReflectionDestroy(shader_reflection);
  }

  XELOGD(
      "MetalShaderConverter: Successfully converted {} bytes DXIL to {} bytes "
      "MetalLib",
      dxil_data.size(), result.metallib_data.size());

  // Cleanup
  IRObjectDestroy(metalObject);
  IRRootSignatureDestroy(rootSig);
  IRCompilerDestroy(compiler);
  IRObjectDestroy(dxilObject);

  result.success = true;
  return true;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
