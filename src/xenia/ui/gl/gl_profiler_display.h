/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_GL_GL_PROFILER_DISPLAY_H_
#define XENIA_UI_GL_GL_PROFILER_DISPLAY_H_

#include "xenia/profiling.h"
#include "xenia/ui/gl/circular_buffer.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace gl {

class GLProfilerDisplay : public ProfilerDisplay {
 public:
  GLProfilerDisplay(xe::ui::Window* window);
  virtual ~GLProfilerDisplay();

  uint32_t width() const override;
  uint32_t height() const override;

  // TODO(benvanik): GPU timestamping.

  void Begin() override;
  void End() override;
  void DrawBox(int x0, int y0, int x1, int y1, uint32_t color,
               BoxType type) override;
  void DrawLine2D(uint32_t count, float* vertices, uint32_t color) override;
  void DrawText(int x, int y, uint32_t color, const char* text,
                size_t text_length) override;

 private:
  struct Vertex {
    float x;
    float y;
    uint32_t color;
    float u;
    float v;
  };

  bool SetupFont();
  bool SetupState();
  bool SetupShaders();

  Vertex* BeginVertices(size_t count);
  void EndVertices(GLenum prim_type);
  void Flush();

  xe::ui::Window* window_ = nullptr;
  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint font_texture_ = 0;
  GLuint64 font_handle_ = 0;
  CircularBuffer vertex_buffer_;

  static const size_t kMaxCommands = 32;
  struct {
    GLenum prim_type;
    size_t vertex_offset;
    size_t vertex_count;
  } draw_commands_[kMaxCommands];
  uint32_t draw_command_count_ = 0;

  CircularBuffer::Allocation current_allocation_;

  struct {
    uint16_t char_offsets[256];
  } font_description_ = {{0}};
};

}  // namespace gl
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_GL_GL_PROFILER_DISPLAY_H_
