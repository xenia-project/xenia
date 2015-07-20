/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GL4_ELEMENTAL_RENDERER_H_
#define XENIA_UI_GL_GL4_ELEMENTAL_RENDERER_H_

#include <cstdint>
#include <memory>

#include "el/graphics/renderer.h"
#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace ui {
namespace gl {

class GL4ElementalRenderer : public el::graphics::Renderer {
 public:
  GL4ElementalRenderer(GLContext* context);
  ~GL4ElementalRenderer() override;

  static std::unique_ptr<GL4ElementalRenderer> Create(GLContext* context);

  void BeginPaint(int render_target_w, int render_target_h) override;
  void EndPaint() override;

  std::unique_ptr<el::graphics::Bitmap> CreateBitmap(int width, int height,
                                                     uint32_t* data) override;

  void RenderBatch(Batch* batch) override;
  void set_clip_rect(const el::Rect& rect) override;

 private:
  class GL4Bitmap : public el::graphics::Bitmap {
   public:
    GL4Bitmap(GLContext* context, GL4ElementalRenderer* renderer);
    ~GL4Bitmap();

    bool Init(int width, int height, uint32_t* data);
    int width() override { return width_; }
    int height() override { return height_; }
    void set_data(uint32_t* data) override;

    GLContext* context_ = nullptr;
    GL4ElementalRenderer* renderer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    GLuint handle_ = 0;
    GLuint64 gpu_handle_ = 0;
  };

  static const uint32_t kMaxVertexBatchSize = 6 * 2048;

  bool Initialize();
  void Flush();

  size_t max_vertex_batch_size() const override { return kMaxVertexBatchSize; }

  GLContext* context_ = nullptr;

  GLuint program_ = 0;
  GLuint vao_ = 0;
  CircularBuffer vertex_buffer_;

  static const size_t kMaxCommands = 512;
  struct {
    GLenum prim_type;
    size_t vertex_offset;
    size_t vertex_count;
  } draw_commands_[kMaxCommands] = {{0}};
  uint32_t draw_command_count_ = 0;
  GL4Bitmap* current_bitmap_ = nullptr;
  Vertex vertices_[kMaxVertexBatchSize];
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL4_ELEMENTAL_RENDERER_H_
