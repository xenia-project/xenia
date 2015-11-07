/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMMEDIATE_DRAWER_H_
#define XENIA_UI_IMMEDIATE_DRAWER_H_

#include <memory>

namespace xe {
namespace ui {

class GraphicsContext;

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
  // Internal handle. Can be passed with the ImmediateDrawBatch.
  uintptr_t handle;

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
  // This is most commonly the handle of an ImmediateTexture.
  uintptr_t texture_handle = 0;
  // Any samples outside of [0-1] on uv will be ignored.
  bool restrict_texture_samples = false;

  // True to enable scissoring using the region defined by scissor_rect.
  bool scissor = false;
  // Scissoring region in framebuffer pixels as (x, y, w, h).
  int scissor_rect[4] = {0};
};

class ImmediateDrawer {
 public:
  virtual ~ImmediateDrawer() = default;

  // Creates a new texture with the given attributes and optionally updates
  // initial data.
  virtual std::unique_ptr<ImmediateTexture> CreateTexture(
      uint32_t width, uint32_t height, ImmediateTextureFilter filter,
      bool repeat, const uint8_t* data = nullptr) = 0;
  // Uploads data to the given texture, replacing the current contents.
  virtual void UpdateTexture(ImmediateTexture* texture,
                             const uint8_t* data) = 0;

  // Begins drawing in immediate mode using the given projection matrix.
  virtual void Begin(int render_target_width, int render_target_height) = 0;
  // Starts a draw batch.
  virtual void BeginDrawBatch(const ImmediateDrawBatch& batch) = 0;
  // Draws one set of a batch.
  virtual void Draw(const ImmediateDraw& draw) = 0;
  // Ends a draw batch.
  virtual void EndDrawBatch() = 0;
  // Ends drawing in immediate mode and flushes contents.
  virtual void End() = 0;

 protected:
  ImmediateDrawer(GraphicsContext* graphics_context)
      : graphics_context_(graphics_context) {}

  GraphicsContext* graphics_context_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMMEDIATE_DRAWER_H_
