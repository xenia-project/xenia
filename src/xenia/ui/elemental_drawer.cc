/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/elemental_drawer.h"

#include "el/graphics/bitmap_fragment.h"
#include "el/util/math.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

ElementalDrawer::ElementalBitmap::ElementalBitmap(ElementalDrawer* drawer,
                                                  int width, int height,
                                                  uint32_t* data)
    : drawer_(drawer) {
  assert(width == el::util::GetNearestPowerOfTwo(width));
  assert(height == el::util::GetNearestPowerOfTwo(height));
  width_ = width;
  height_ = height;

  drawer_->FlushBitmap(this);

  auto immediate_drawer = drawer_->graphics_context_->immediate_drawer();
  texture_ = immediate_drawer->CreateTexture(
      width, height, ImmediateTextureFilter::kLinear, true,
      reinterpret_cast<uint8_t*>(data));
}

ElementalDrawer::ElementalBitmap::~ElementalBitmap() {
  // Must flush and unbind before we delete the texture.
  drawer_->FlushBitmap(this);
}

void ElementalDrawer::ElementalBitmap::set_data(uint32_t* data) {
  drawer_->FlushBitmap(this);

  auto immediate_drawer = drawer_->graphics_context_->immediate_drawer();
  immediate_drawer->UpdateTexture(texture_.get(),
                                  reinterpret_cast<uint8_t*>(data));
}

ElementalDrawer::ElementalDrawer(xe::ui::Window* window)
    : window_(window), graphics_context_(window->context()) {
  static_assert(sizeof(ImmediateVertex) == sizeof(Vertex),
                "Vertex types must match");
}

ElementalDrawer::~ElementalDrawer() = default;

void ElementalDrawer::BeginPaint(int render_target_w, int render_target_h) {
  Renderer::BeginPaint(render_target_w, render_target_h);

  auto immediate_drawer = graphics_context_->immediate_drawer();
  immediate_drawer->Begin(render_target_w, render_target_h);

  batch_.vertices = vertices_;
}

void ElementalDrawer::EndPaint() {
  Renderer::EndPaint();

  auto immediate_drawer = graphics_context_->immediate_drawer();
  immediate_drawer->End();
}

std::unique_ptr<el::graphics::Bitmap> ElementalDrawer::CreateBitmap(
    int width, int height, uint32_t* data) {
  auto bitmap = std::make_unique<ElementalBitmap>(this, width, height, data);
  return std::unique_ptr<el::graphics::Bitmap>(bitmap.release());
}

void ElementalDrawer::RenderBatch(Batch* batch) {
  auto immediate_drawer = graphics_context_->immediate_drawer();

  ImmediateDrawBatch draw_batch;

  draw_batch.vertices = reinterpret_cast<ImmediateVertex*>(batch->vertices);
  draw_batch.vertex_count = static_cast<int>(batch->vertex_count);
  immediate_drawer->BeginDrawBatch(draw_batch);

  ImmediateDraw draw;
  draw.primitive_type = ImmediatePrimitiveType::kTriangles;
  draw.count = static_cast<int>(batch->vertex_count);
  auto bitmap = static_cast<ElementalBitmap*>(batch->bitmap);
  draw.texture_handle = bitmap ? bitmap->texture_->handle : 0;
  draw.scissor = current_clip_.w && current_clip_.h;
  draw.scissor_rect[0] = current_clip_.x;
  draw.scissor_rect[1] =
      window_->height() - (current_clip_.y + current_clip_.h);
  draw.scissor_rect[2] = current_clip_.w;
  draw.scissor_rect[3] = current_clip_.h;
  immediate_drawer->Draw(draw);

  immediate_drawer->EndDrawBatch();
}

void ElementalDrawer::set_clip_rect(const el::Rect& rect) {
  current_clip_ = rect;
}

}  // namespace ui
}  // namespace xe
