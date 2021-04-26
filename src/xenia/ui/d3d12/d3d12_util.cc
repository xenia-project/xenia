/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_util.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace d3d12 {
namespace util {

const D3D12_HEAP_PROPERTIES kHeapPropertiesDefault = {D3D12_HEAP_TYPE_DEFAULT};
const D3D12_HEAP_PROPERTIES kHeapPropertiesUpload = {D3D12_HEAP_TYPE_UPLOAD};
const D3D12_HEAP_PROPERTIES kHeapPropertiesReadback = {
    D3D12_HEAP_TYPE_READBACK};

ID3D12RootSignature* CreateRootSignature(
    const D3D12Provider& provider, const D3D12_ROOT_SIGNATURE_DESC& desc) {
  ID3DBlob* blob;
  ID3DBlob* error_blob = nullptr;
  if (FAILED(provider.SerializeRootSignature(
          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error_blob))) {
    XELOGE("Failed to serialize a root signature");
    if (error_blob != nullptr) {
      XELOGE("{}",
             reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
      error_blob->Release();
    }
    return nullptr;
  }
  if (error_blob != nullptr) {
    error_blob->Release();
  }
  ID3D12RootSignature* root_signature = nullptr;
  provider.GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature));
  blob->Release();
  return root_signature;
}

ID3D12PipelineState* CreateComputePipeline(
    ID3D12Device* device, const void* shader, size_t shader_size,
    ID3D12RootSignature* root_signature) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
  desc.pRootSignature = root_signature;
  desc.CS.pShaderBytecode = shader;
  desc.CS.BytecodeLength = shader_size;
  desc.NodeMask = 0;
  desc.CachedPSO.pCachedBlob = nullptr;
  desc.CachedPSO.CachedBlobSizeInBytes = 0;
  desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  ID3D12PipelineState* pipeline = nullptr;
  device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
  return pipeline;
}

void CreateBufferRawSRV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset) {
  assert_false(size & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  assert_false(offset & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = offset >> 2;
  desc.Buffer.NumElements = size >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  device->CreateShaderResourceView(buffer, &desc, handle);
}

void CreateBufferRawUAV(ID3D12Device* device,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle,
                        ID3D12Resource* buffer, uint32_t size,
                        uint64_t offset) {
  assert_false(size & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  assert_false(offset & (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT - 1));
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
  desc.Format = DXGI_FORMAT_R32_TYPELESS;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = offset >> 2;
  desc.Buffer.NumElements = size >> 2;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.CounterOffsetInBytes = 0;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  device->CreateUnorderedAccessView(buffer, nullptr, &desc, handle);
}

void CreateBufferTypedSRV(ID3D12Device* device,
                          D3D12_CPU_DESCRIPTOR_HANDLE handle,
                          ID3D12Resource* buffer, DXGI_FORMAT format,
                          uint32_t num_elements, uint64_t first_element) {
  D3D12_SHADER_RESOURCE_VIEW_DESC desc;
  desc.Format = format;
  desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  desc.Buffer.FirstElement = first_element;
  desc.Buffer.NumElements = num_elements;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  device->CreateShaderResourceView(buffer, &desc, handle);
}

void CreateBufferTypedUAV(ID3D12Device* device,
                          D3D12_CPU_DESCRIPTOR_HANDLE handle,
                          ID3D12Resource* buffer, DXGI_FORMAT format,
                          uint32_t num_elements, uint64_t first_element) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
  desc.Format = format;
  desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  desc.Buffer.FirstElement = first_element;
  desc.Buffer.NumElements = num_elements;
  desc.Buffer.StructureByteStride = 0;
  desc.Buffer.CounterOffsetInBytes = 0;
  desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  device->CreateUnorderedAccessView(buffer, nullptr, &desc, handle);
}

void GetFormatCopyInfo(DXGI_FORMAT format, uint32_t plane,
                       DXGI_FORMAT& copy_format_out, uint32_t& block_width_out,
                       uint32_t& block_height_out,
                       uint32_t& bytes_per_block_out) {
  DXGI_FORMAT copy_format = format;
  uint32_t block_width = 1;
  uint32_t block_height = 1;
  bool divide_by_block_size = false;
  uint32_t bytes_per_block = 1;
  switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
      bytes_per_block = 16;
      break;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
      bytes_per_block = 12;
      break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_Y416:
      bytes_per_block = 8;
      break;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
      if (plane) {
        copy_format = DXGI_FORMAT_R8_TYPELESS;
        bytes_per_block = 1;
      } else {
        copy_format = DXGI_FORMAT_R32_TYPELESS;
        bytes_per_block = 4;
      }
      break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
      bytes_per_block = 4;
      break;
    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      bytes_per_block = 2;
      break;
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
      bytes_per_block = 1;
      break;
    // R1_UNORM is not supported in Direct3D 12.
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
      // Failed to GetCopyableFootprints for Y210 and Y216 on Intel UHD Graphics
      // 630.
      block_width = 2;
      bytes_per_block = 4;
      break;
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
      block_width = 4;
      block_height = 4;
      bytes_per_block = 8;
      break;
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
      block_width = 4;
      block_height = 4;
      bytes_per_block = 16;
      break;
    // NV12, P010, P016, 420_OPAQUE and NV11 are not handled here because of
    // differences that need to be handled externally.
    // For future reference, if needed:
    // - Width and height of planes 1 and 2 are divided by the block size in the
    //   footprint itself (unlike in block-compressed textures, where the
    //   dimensions are merely aligned).
    // - Rows are aligned to the placement alignment (512) rather than the pitch
    //   alignment (256) for some reason (to match the Direct3D 11 layout
    //   without explicit planes, requiring the plane data to be laid out in
    //   some specific way defined on MSDN within each row, though Direct3D 12
    //   possibly doesn't have such requirement, but investigation needed.
    // - NV12: R8_TYPELESS plane 0, R8G8_TYPELESS plane 1.
    // - P010, P016: R16_TYPELESS plane 0, R16G16_TYPELESS plane 1. Failed to
    //   GetCopyableFootprints for P016 on Nvidia GeForce GTX 1070.
    // - 420_OPAQUE: Single R8_TYPELESS plane.
    // - NV11: Failed to GetCopyableFootprints on both Nvidia GeForce GTX 1070
    //   and Intel UHD Graphics 630.
    case DXGI_FORMAT_YUY2:
      block_width = 2;
      bytes_per_block = 2;
      break;
    // P208, V208 and V408 are not supported in Direct3D 12.
    default:
      assert_unhandled_case(format);
  }
  copy_format_out = copy_format;
  block_width_out = block_width;
  block_height_out = block_height;
  bytes_per_block_out = bytes_per_block;
}

}  // namespace util
}  // namespace d3d12
}  // namespace ui
}  // namespace xe
