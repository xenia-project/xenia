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
void* MetalShaderConverter::CreateXbox360RootSignature(MetalShaderStage stage) {
  IRShaderVisibility visibility;
  switch (stage) {
    case MetalShaderStage::kVertex:
      visibility = IRShaderVisibilityVertex;
      break;
    case MetalShaderStage::kFragment:
      visibility = IRShaderVisibilityPixel;
      break;
    case MetalShaderStage::kCompute:
      visibility = IRShaderVisibilityAll;
      break;
    default:
      visibility = IRShaderVisibilityAll;
      break;
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

  // Create and set Xbox 360 root signature
  IRRootSignature* rootSig =
      static_cast<IRRootSignature*>(CreateXbox360RootSignature(stage));
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

  // Determine which shader stage to extract
  IRShaderStage irStage;
  switch (stage) {
    case MetalShaderStage::kVertex:
      irStage = IRShaderStageVertex;
      result.function_name = "vertexMain";
      break;
    case MetalShaderStage::kFragment:
      irStage = IRShaderStageFragment;
      result.function_name = "fragmentMain";
      break;
    case MetalShaderStage::kCompute:
      irStage = IRShaderStageCompute;
      result.function_name = "computeMain";
      break;
    default:
      irStage = IRShaderStageFragment;
      result.function_name = "fragmentMain";
      break;
  }

  // Extract MetalLib binary
  IRMetalLibBinary* metallib = IRMetalLibBinaryCreate();
  IRObjectGetMetalLibBinary(metalObject, irStage, metallib);

  size_t metallibSize = IRMetalLibGetBytecodeSize(metallib);
  if (metallibSize == 0) {
    result.success = false;
    result.error_message = "Generated MetalLib has zero size";
    IRMetalLibBinaryDestroy(metallib);
    IRObjectDestroy(metalObject);
    IRRootSignatureDestroy(rootSig);
    IRCompilerDestroy(compiler);
    IRObjectDestroy(dxilObject);
    return false;
  }

  result.metallib_data.resize(metallibSize);
  IRMetalLibGetBytecode(metallib, result.metallib_data.data());

  XELOGD(
      "MetalShaderConverter: Successfully converted {} bytes DXIL to {} bytes "
      "MetalLib",
      dxil_data.size(), metallibSize);

  // Cleanup
  IRMetalLibBinaryDestroy(metallib);
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
