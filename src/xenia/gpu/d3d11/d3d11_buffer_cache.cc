/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_buffer_cache.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_buffer.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::gpu::xenos;


D3D11BufferCache::D3D11BufferCache(ID3D11DeviceContext* context,
                                   ID3D11Device* device)
    : context_(context), device_(device) {
  context->AddRef();
  device_->AddRef();
}

D3D11BufferCache::~D3D11BufferCache() {
  XESAFERELEASE(device_);
  XESAFERELEASE(context_);
}

IndexBuffer* D3D11BufferCache::CreateIndexBuffer(
    const IndexBufferInfo& info,
    const uint8_t* src_ptr, size_t length) {
  return new D3D11IndexBuffer(this, info, src_ptr, length);
}
