/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_TEXTURE_H_
#define XENIA_GPU_D3D11_D3D11_TEXTURE_H_

#include <xenia/core.h>

#include <xenia/gpu/texture.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11TextureCache;


struct D3D11TextureView : TextureView {
  ID3D11Resource* resource;
  ID3D11ShaderResourceView* srv;

  D3D11TextureView()
      : resource(nullptr), srv(nullptr) {}
  virtual ~D3D11TextureView() {
    XESAFERELEASE(srv);
    XESAFERELEASE(resource);
  }
};


class D3D11Texture : public Texture {
public:
  D3D11Texture(D3D11TextureCache* cache, uint32_t address,
               const uint8_t* host_address);
  virtual ~D3D11Texture();

protected:
  TextureView* FetchNew(
      const xenos::xe_gpu_texture_fetch_t& fetch) override;
  bool FetchDirty(
      TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch) override;

  bool CreateTexture1D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool FetchTexture1D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool CreateTexture2D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool FetchTexture2D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool CreateTexture3D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool FetchTexture3D(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool CreateTextureCube(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);
  bool FetchTextureCube(
      D3D11TextureView* view, const xenos::xe_gpu_texture_fetch_t& fetch);

  D3D11TextureCache* cache_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_TEXTURE_H_
