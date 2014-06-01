/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_buffer.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_buffer_cache.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11IndexBuffer::D3D11IndexBuffer(
    D3D11BufferCache* buffer_cache,
    const IndexBufferInfo& info,
    const uint8_t* src_ptr, size_t length)
    : IndexBuffer(info, src_ptr, length),
      buffer_cache_(buffer_cache),
      handle_(nullptr) {
}

D3D11IndexBuffer::~D3D11IndexBuffer() {
  XESAFERELEASE(handle_);
}

bool D3D11IndexBuffer::FetchNew(uint64_t hash) {
  hash_ = hash;

  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = info_.index_size;
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = buffer_cache_->device()->CreateBuffer(&buffer_desc, NULL, &handle_);
  if (FAILED(hr)) {
    XELOGW("D3D11: failed to create index buffer");
    return false;
  }

  return FetchDirty(hash);
}

bool D3D11IndexBuffer::FetchDirty(uint64_t hash) {
  hash_ = hash;
  
  // All that's done so far:
  XEASSERT(info_.endianness == 0x2);

  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = buffer_cache_->context()->Map(
      handle_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to map index buffer");
    return false;
  }

  if (info_.index_32bit) {
    const uint32_t* src = reinterpret_cast<const uint32_t*>(src_);
    uint32_t* dest = reinterpret_cast<uint32_t*>(res.pData);
    for (uint32_t n = 0; n < info_.index_count; n++) {
      uint32_t d = { XESWAP32(src[n]) };
      dest[n] = d;
    }
  } else {
    const uint16_t* src = reinterpret_cast<const uint16_t*>(src_);
    uint16_t* dest = reinterpret_cast<uint16_t*>(res.pData);
    for (uint32_t n = 0; n < info_.index_count; n++) {
      uint16_t d = XESWAP16(src[n]);
      dest[n] = d;
    }
  }
  buffer_cache_->context()->Unmap(handle_, 0);

  return true;
}


D3D11VertexBuffer::D3D11VertexBuffer(
    D3D11BufferCache* buffer_cache,
    const VertexBufferInfo& info,
    const uint8_t* src_ptr, size_t length)
    : VertexBuffer(info, src_ptr, length),
      buffer_cache_(buffer_cache),
      handle_(nullptr) {
}

D3D11VertexBuffer::~D3D11VertexBuffer() {
  XESAFERELEASE(handle_);
}

bool D3D11VertexBuffer::FetchNew(uint64_t hash) {
  hash_ = hash;

  D3D11_BUFFER_DESC buffer_desc;
  xe_zero_struct(&buffer_desc, sizeof(buffer_desc));
  buffer_desc.ByteWidth       = static_cast<UINT>(length_);
  buffer_desc.Usage           = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags       = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = buffer_cache_->device()->CreateBuffer(&buffer_desc, NULL, &handle_);
  if (FAILED(hr)) {
    XELOGW("D3D11: failed to create index buffer");
    return false;
  }

  return FetchDirty(hash);
}

bool D3D11VertexBuffer::FetchDirty(uint64_t hash) {
  hash_ = hash;

  D3D11_MAPPED_SUBRESOURCE res;
  HRESULT hr = buffer_cache_->context()->Map(
      handle_, 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
  if (FAILED(hr)) {
    XELOGE("D3D11: unable to map vertex buffer");
    return false;
  }
  uint8_t* dest = reinterpret_cast<uint8_t*>(res.pData);

  // TODO(benvanik): rewrite to be faster/special case common/etc
  uint32_t stride = info_.layout.stride_words;
  size_t count = (length_ / 4) / stride;
  for (size_t n = 0; n < info_.layout.element_count; n++) {
    const auto& el = info_.layout.elements[n];
    const uint32_t* src_ptr = (const uint32_t*)(src_ + el.offset_words * 4);
    uint32_t* dest_ptr = (uint32_t*)(dest + el.offset_words * 4);
    uint32_t o = 0;
    for (uint32_t i = 0; i < count; i++) {
      for (uint32_t j = 0; j < el.size_words; j++) {
        dest_ptr[o + j] = XESWAP32(src_ptr[o + j]);
      }
      o += stride;
    }
  }


  buffer_cache_->context()->Unmap(handle_, 0);
  return true;
}
