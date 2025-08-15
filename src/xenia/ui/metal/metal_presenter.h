/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_PRESENTER_H_
#define XENIA_UI_METAL_METAL_PRESENTER_H_

#include <array>

#include "xenia/ui/metal/metal_provider.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/surface.h"

// Forward declaration for CAMetalLayer
#ifdef __OBJC__
@class CAMetalLayer;
@protocol MTLCommandQueue;
@protocol MTLTexture;
#else
typedef struct objc_object CAMetalLayer;
typedef struct objc_object* id;
#endif

namespace xe {
namespace ui {
namespace metal {

class MetalProvider;

class MetalGuestOutputRefreshContext final : public Presenter::GuestOutputRefreshContext {
 public:
  MetalGuestOutputRefreshContext(bool& is_8bpc_out_ref, id resource)
      : Presenter::GuestOutputRefreshContext(is_8bpc_out_ref), resource_(resource) {}

  // The guest output Metal texture that the refresher should write to.
  // Initial state is undefined, refresher must write all pixels.
  id resource_uav_capable() const { return resource_; }

 private:
  id resource_;
};

class MetalUIDrawContext final : public UIDrawContext {
 public:
  MetalUIDrawContext(Presenter& presenter, uint32_t render_target_width,
                     uint32_t render_target_height,
                     id command_buffer,  // id<MTLCommandBuffer>
                     id render_encoder)  // id<MTLRenderCommandEncoder>
      : UIDrawContext(presenter, render_target_width, render_target_height),
        command_buffer_(command_buffer),
        render_encoder_(render_encoder) {}

  id command_buffer() const { return command_buffer_; }  // id<MTLCommandBuffer>
  id render_encoder() const { return render_encoder_; }  // id<MTLRenderCommandEncoder>

 private:
  id command_buffer_;  // id<MTLCommandBuffer>
  id render_encoder_;  // id<MTLRenderCommandEncoder>
};

class MetalPresenter : public Presenter {
 public:
  MetalPresenter(MetalProvider* provider, HostGpuLossCallback host_gpu_loss_callback);
  ~MetalPresenter() override;

  bool Initialize();
  void Shutdown();

  Surface::TypeFlags GetSupportedSurfaceTypes() const override;
  bool CaptureGuestOutput(RawImage& image_out) override;

  // Helper method to copy Metal texture to guest output texture
  bool CopyTextureToGuestOutput(MTL::Texture* source_texture, id dest_texture);
  
  // Helper method for trace dumps to populate guest output before PNG capture
  void TryRefreshGuestOutputForTraceDump(void* command_processor);

 protected:
  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;
  
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width,
      uint32_t new_surface_height, bool was_paintable,
      bool& is_vsync_implicit_out) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;
  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out_ref) override;

 private:
  MetalProvider* provider_;
  MTL::Device* device_ = nullptr;
  
  // Metal presentation resources
  CAMetalLayer* metal_layer_ = nullptr;
  id command_queue_ = nullptr;  // id<MTLCommandQueue>
  
  // Guest output textures for PNG capture (mailbox system)
  std::array<id, kGuestOutputMailboxSize> guest_output_textures_;
};

}  // namespace metal
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_METAL_METAL_PRESENTER_H_
