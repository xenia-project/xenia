/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_buffer_resource.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_resource_cache.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11IndexBufferResource::D3D11IndexBufferResource(
    D3D11ResourceCache* resource_cache,
    const MemoryRange& memory_range,
    const Info& info)
    : IndexBufferResource(memory_range, info),
      resource_cache_(resource_cache),
      handle_(nullptr) {
}

D3D11IndexBufferResource::~D3D11IndexBufferResource() {
  XESAFERELEASE(handle_);
}

int D3D11IndexBufferResource::CreateHandle() {
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = static_cast<UINT>(memory_range_.length);
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = resource_cache_->device()->CreateBuffer(
      &buffer_desc, nullptr, &handle_);
  if (FAILED(hr)) {
    XELOGW("D3D11: failed to create index buffer");
    return 1;
  }
  return 0;
}

int D3D11IndexBufferResource::InvalidateRegion(
    const MemoryRange& memory_range) {
  SCOPE_profile_cpu_f("gpu");

  // All that's done so far:
  assert_true(info_.endianness == 0x2);

  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = resource_cache_->context()->Map(
      handle_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to map index buffer");
    return 1;
  }

  if (info_.format == INDEX_FORMAT_32BIT) {
    uint32_t index_count = memory_range_.length / 4;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(
        memory_range_.host_base);
    uint32_t* dest = reinterpret_cast<uint32_t*>(res.pData);
    for (uint32_t n = 0; n < index_count; n++) {
      dest[n] = poly::byte_swap(src[n]);
    }
  } else {
    uint32_t index_count = memory_range_.length / 2;
    const uint16_t* src = reinterpret_cast<const uint16_t*>(
        memory_range_.host_base);
    uint16_t* dest = reinterpret_cast<uint16_t*>(res.pData);
    for (uint32_t n = 0; n < index_count; n++) {
      dest[n] = poly::byte_swap(src[n]);
    }
  }
  resource_cache_->context()->Unmap(handle_, 0);

  return 0;
}

D3D11VertexBufferResource::D3D11VertexBufferResource(
    D3D11ResourceCache* resource_cache,
    const MemoryRange& memory_range,
    const Info& info)
    : VertexBufferResource(memory_range, info),
      resource_cache_(resource_cache),
      handle_(nullptr) {
}

D3D11VertexBufferResource::~D3D11VertexBufferResource() {
  XESAFERELEASE(handle_);
}

int D3D11VertexBufferResource::CreateHandle() {
  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = static_cast<UINT>(memory_range_.length);
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = resource_cache_->device()->CreateBuffer(
      &buffer_desc, nullptr, &handle_);
  if (FAILED(hr)) {
    XELOGW("D3D11: failed to create vertex buffer");
    return 1;
  }
  return 0;
}

int D3D11VertexBufferResource::InvalidateRegion(
    const MemoryRange& memory_range) {
  SCOPE_profile_cpu_f("gpu");

  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = resource_cache_->context()->Map(
      handle_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to map vertex buffer");
    return 1;
  }
  uint8_t* dest = reinterpret_cast<uint8_t*>(res.pData);

  // TODO(benvanik): rewrite to be faster/special case common/etc
  uint32_t stride = info_.stride_words;
  size_t count = (memory_range_.length / 4) / stride;
  for (size_t n = 0; n < info_.element_count; n++) {
    const auto& el = info_.elements[n];
    const uint32_t* src_ptr = (const uint32_t*)(
        memory_range_.host_base + el.offset_words * 4);
    uint32_t* dest_ptr = (uint32_t*)(dest + el.offset_words * 4);
    uint32_t o = 0;
    for (uint32_t i = 0; i < count; i++) {
      for (uint32_t j = 0; j < el.size_words; j++) {
        dest_ptr[o + j] = poly::byte_swap(src_ptr[o + j]);
      }
      o += stride;
    }
  }

  resource_cache_->context()->Unmap(handle_, 0);
  return 0;
}
