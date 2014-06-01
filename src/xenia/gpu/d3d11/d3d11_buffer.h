/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_BUFFER_H_
#define XENIA_GPU_D3D11_D3D11_BUFFER_H_

#include <xenia/core.h>

#include <xenia/gpu/buffer.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11BufferCache;


class D3D11IndexBuffer : public IndexBuffer {
public:
  D3D11IndexBuffer(D3D11BufferCache* buffer_cache,
                   const IndexBufferInfo& info,
                   const uint8_t* src_ptr, size_t length);
  virtual ~D3D11IndexBuffer();

  ID3D11Buffer* handle() const { return handle_; }

  bool FetchNew(uint64_t hash) override;
  bool FetchDirty(uint64_t hash) override;

private:
  D3D11BufferCache* buffer_cache_;
  ID3D11Buffer* handle_;
};


class D3D11VertexBuffer : public VertexBuffer {
public:
private:
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_BUFFER_H_
