/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMMEDIATE_DRAWER_H_
#define XENIA_UI_IMMEDIATE_DRAWER_H_

#include <memory>

#include "xenia/ui/presenter.h"

namespace xe {
namespace ui {

// Describes the filter applied when sampling textures.
enum class ImmediateTextureFilter {
  kNearest,
  kLinear,
};

// Simple texture compatible with the immediate renderer.
class ImmediateTexture {
 public:
  virtual ~ImmediateTexture() = default;

  // Texture width, in pixels.
  uint32_t width;
  // Texture height, in pixels.
  uint32_t height;

 protected:
  ImmediateTexture(uint32_t width, uint32_t height)
      : width(width), height(height) {}
};

// Describes the primitive type used by a draw call.
enum class ImmediatePrimitiveType {
  kLines,
  kTriangles,
};

// Simple vertex used by the immediate mode drawer.
// To avoid translations, this matches both imgui and elemental-forms vertices:
//   ImDrawVert
//   el::graphics::Renderer::Vertex
struct ImmediateVertex {
  float x, y;
  float u, v;
  uint32_t color;
};

// All parameters required to draw an immediate-mode batch of vertices.
struct ImmediateDrawBatch {
  // Vertices to draw.
  const ImmediateVertex* vertices = nullptr;
  int vertex_count = 0;

  // Optional index buffer indices.
  const uint16_t* indices = nullptr;
  int index_count = 0;
};

struct ImmediateDraw {
  // Primitive type the vertices/indices represent.
  ImmediatePrimitiveType primitive_type = ImmediatePrimitiveType::kTriangles;
  // Total number of elements to draw.
  int count = 0;
  // Starting offset in the index buffer.
  int index_offset = 0;
  // Base vertex of elements, if using an index buffer.
  int base_vertex = 0;

  // Texture used when drawing, or nullptr if color only.
  ImmediateTexture* texture = nullptr;

  // True to enable scissoring using the region defined by scissor_rect.
  bool scissor = false;
  // Scissoring region in the coordinate space (if right < left or bottom < top,
  // not drawing).
  float scissor_left = 0.0f;
  float scissor_top = 0.0f;
  float scissor_right = 0.0f;
  float scissor_bottom = 0.0f;
};

class ImmediateDrawer {
 public:
  ImmediateDrawer(const ImmediateDrawer& immediate_drawer) = delete;
  ImmediateDrawer& operator=(const ImmediateDrawer& immediate_drawer) = delete;

  virtual ~ImmediateDrawer() = default;

  void SetPresenter(Presenter* new_presenter);

  // Creates a new texture with the given attributes and R8G8B8A8 data.
  virtual std::unique_ptr<ImmediateTexture> CreateTexture(
      uint32_t width, uint32_t height, ImmediateTextureFilter filter,
      bool is_repeated, const uint8_t* data) = 0;

  // Begins drawing in immediate mode using the given projection matrix. The
  // presenter that is currently attached to the immediate drawer, as the
  // implementation may hold presenter-specific information such as UI
  // submission indices. Pass 0 or a negative value as the coordinate space
  // width or height to use raw render target pixel coordinates (or this will
  // just be used as a safe fallback when with a non-zero-sized surface the
  // coordinate space size becomes zero somehow).
  virtual void Begin(UIDrawContext& ui_draw_context,
                     float coordinate_space_width,
                     float coordinate_space_height);
  // Starts a draw batch.
  virtual void BeginDrawBatch(const ImmediateDrawBatch& batch) = 0;
  // Draws one set of a batch.
  virtual void Draw(const ImmediateDraw& draw) = 0;
  // Ends a draw batch.
  virtual void EndDrawBatch() = 0;
  // Ends drawing in immediate mode and flushes contents.
  virtual void End();

 protected:
  ImmediateDrawer() = default;

  Presenter* presenter() const { return presenter_; }
  virtual void OnLeavePresenter() {}
  virtual void OnEnterPresenter() {}

  // Available between Begin and End.
  UIDrawContext* ui_draw_context() const { return ui_draw_context_; }
  float coordinate_space_width() const { return coordinate_space_width_; }
  float coordinate_space_height() const { return coordinate_space_height_; }

  // Converts and clamps the scissor in the immediate draw to render target
  // coordinates. Returns whether the scissor contains any render target pixels
  // (but a valid scissor is written even if false is returned).
  bool ScissorToRenderTarget(const ImmediateDraw& immediate_draw,
                             uint32_t& out_left, uint32_t& out_top,
                             uint32_t& out_width, uint32_t& out_height);

 private:
  Presenter* presenter_ = nullptr;

  UIDrawContext* ui_draw_context_ = nullptr;
  float coordinate_space_width_;
  float coordinate_space_height_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMMEDIATE_DRAWER_H_
