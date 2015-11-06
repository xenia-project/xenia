/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_MICROPROFILE_DRAWER_H_
#define XENIA_UI_MICROPROFILE_DRAWER_H_

#include <memory>
#include <vector>

#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace ui {

class GraphicsContext;
class Window;

class MicroprofileDrawer {
 public:
  enum class BoxType {
    kBar = 0,   // MicroProfileBoxTypeBar
    kFlat = 1,  // MicroProfileBoxTypeFlat
  };

  MicroprofileDrawer(Window* window);
  ~MicroprofileDrawer();

  void Begin();
  void End();
  void DrawBox(int x0, int y0, int x1, int y1, uint32_t color, BoxType type);
  void DrawLine2D(uint32_t count, float* vertices, uint32_t color);
  void DrawText(int x, int y, uint32_t color, const char* text,
                int text_length);

 protected:
  void SetupFont();

  ImmediateVertex* BeginVertices(ImmediatePrimitiveType primitive_type,
                                 int count);
  void EndVertices();
  void Flush();

  Window* window_ = nullptr;
  GraphicsContext* graphics_context_ = nullptr;

  std::vector<ImmediateVertex> vertices_;
  int vertex_count_ = 0;
  ImmediatePrimitiveType current_primitive_type_;

  std::unique_ptr<ImmediateTexture> font_texture_;
  struct {
    uint16_t char_offsets[256];
  } font_description_ = {{0}};
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_MICROPROFILE_DRAWER_H_
