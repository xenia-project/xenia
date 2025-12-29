/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_IMMEDIATE_DRAWER_H_
#define XENIA_UI_METAL_METAL_IMMEDIATE_DRAWER_H_

#include "xenia/ui/immediate_drawer.h"

// Forward declarations for Metal types
namespace MTL {
class Device;
class Texture;
class RenderPipelineState;
class CommandBuffer;
class RenderCommandEncoder;
class Buffer;
class SamplerState;
}

namespace xe {
namespace ui {
namespace metal {

class MetalProvider;

class MetalImmediateDrawer : public ImmediateDrawer {
 public:
  explicit MetalImmediateDrawer(MetalProvider* provider);
  ~MetalImmediateDrawer() override;

  bool Initialize();
  void Shutdown();

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool repeat,
                                                  const uint8_t* data) override;
  std::unique_ptr<ImmediateTexture> CreateTextureFromMetal(
      MTL::Texture* texture, MTL::SamplerState* sampler);

  void Begin(UIDrawContext& ui_draw_context,
             float coordinate_space_width,
             float coordinate_space_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  class MetalImmediateTexture : public ImmediateTexture {
   public:
    MetalImmediateTexture(uint32_t width, uint32_t height)
        : ImmediateTexture(width, height) {}
    ~MetalImmediateTexture() override;

    MTL::Texture* texture = nullptr;
    MTL::SamplerState* sampler = nullptr;
  };

  MetalProvider* provider_;
  MTL::Device* device_ = nullptr;
  
  // Rendering pipeline objects
  MTL::RenderPipelineState* pipeline_textured_ = nullptr;
  MTL::Texture* white_texture_ = nullptr;
  MTL::SamplerState* default_sampler_ = nullptr;
  
  // Current rendering state
  MTL::CommandBuffer* current_command_buffer_ = nullptr;
  MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;
  bool batch_open_ = false;
  
  // Current batch index buffer (stored as void* for C++ compatibility)
  void* current_index_buffer_ = nullptr;
};

}  // namespace metal
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_METAL_METAL_IMMEDIATE_DRAWER_H_
