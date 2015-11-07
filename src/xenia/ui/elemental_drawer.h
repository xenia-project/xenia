/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_ELEMENTAL_DRAWER_H_
#define XENIA_UI_ELEMENTAL_DRAWER_H_

#include <cstdint>
#include <memory>

#include "el/graphics/renderer.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {

class GraphicsContext;
class Window;

class ElementalDrawer : public el::graphics::Renderer {
 public:
  ElementalDrawer(Window* window);
  ~ElementalDrawer();

  void BeginPaint(int render_target_w, int render_target_h) override;
  void EndPaint() override;

  std::unique_ptr<el::graphics::Bitmap> CreateBitmap(int width, int height,
                                                     uint32_t* data) override;

  void RenderBatch(Batch* batch) override;
  void set_clip_rect(const el::Rect& rect) override;

 protected:
  class ElementalBitmap : public el::graphics::Bitmap {
   public:
    ElementalBitmap(ElementalDrawer* drawer, int width, int height,
                    uint32_t* data);
    ~ElementalBitmap();

    int width() override { return width_; }
    int height() override { return height_; }
    void set_data(uint32_t* data) override;

    ElementalDrawer* drawer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::unique_ptr<ImmediateTexture> texture_;
  };

  static const uint32_t kMaxVertexBatchSize = 6 * 2048;
  size_t max_vertex_batch_size() const override { return kMaxVertexBatchSize; }

  Window* window_ = nullptr;
  GraphicsContext* graphics_context_ = nullptr;

  el::Rect current_clip_;
  Vertex vertices_[kMaxVertexBatchSize];
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_ELEMENTAL_DRAWER_H_
