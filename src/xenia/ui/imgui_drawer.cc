/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/fonts/koruriregular.h"
#include "xenia/ui/fonts/liberationmonoregular.h"

#include "third_party/imgui/imgui.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {


static_assert(sizeof(ImmediateVertex) == sizeof(ImDrawVert),
              "Vertex types must match");

ImGuiDrawer::ImGuiDrawer(xe::ui::Window* window)
    : window_(window), graphics_context_(window->context()) {
  Initialize();
}

ImGuiDrawer::~ImGuiDrawer() {
  if (internal_state_) {
    ImGui::DestroyContext(internal_state_);
    internal_state_ = nullptr;
  }
}

void ImGuiDrawer::Initialize() {
  // Setup ImGui internal state.
  // This will give us state we can swap to the ImGui globals when in use.
  internal_state_ = ImGui::CreateContext();

  auto& io = ImGui::GetIO();

  // TODO(gibbed): disable imgui.ini saving for now,
  // imgui assumes paths are char* so we can't throw a good path at it on
  // Windows.
  io.IniFilename = nullptr;

  SetupFont();

  io.DeltaTime = 1.0f / 60.0f;

  auto& style = ImGui::GetStyle();
  style.ScrollbarRounding = 0;
  style.WindowRounding = 0;
  style.Colors[ImGuiCol_Text] = ImVec4(0.89f, 0.90f, 0.90f, 1.00f);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.06f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.40f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.33f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.65f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.35f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.40f, 0.11f, 0.59f);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.68f, 0.00f, 0.68f);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] =
      ImVec4(0.00f, 1.00f, 0.15f, 0.62f);
  style.Colors[ImGuiCol_ScrollbarGrabActive] =
      ImVec4(0.00f, 0.91f, 0.09f, 0.40f);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.99f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.90f, 0.72f, 0.50f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.75f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.56f, 0.11f, 0.60f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.72f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.60f, 0.09f, 1.00f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.00f, 0.71f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.60f, 0.26f, 0.80f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.75f, 0.00f, 0.80f);
  style.Colors[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.36f, 0.89f, 0.38f, 1.00f);
  style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.13f, 0.50f, 0.11f, 1.00f);
  style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
  style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
  style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
  style.Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_PlotHistogramHovered] =
      ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
  style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 0.00f, 0.21f);
  style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

  io.KeyMap[ImGuiKey_Tab] = 0x09;  // VK_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = 0x25;
  io.KeyMap[ImGuiKey_RightArrow] = 0x27;
  io.KeyMap[ImGuiKey_UpArrow] = 0x26;
  io.KeyMap[ImGuiKey_DownArrow] = 0x28;
  io.KeyMap[ImGuiKey_Home] = 0x24;
  io.KeyMap[ImGuiKey_End] = 0x23;
  io.KeyMap[ImGuiKey_Delete] = 0x2E;
  io.KeyMap[ImGuiKey_Backspace] = 0x08;
  io.KeyMap[ImGuiKey_Enter] = 0x0D;
  io.KeyMap[ImGuiKey_Escape] = 0x1B;
  io.KeyMap[ImGuiKey_A] = 'A';
  io.KeyMap[ImGuiKey_C] = 'C';
  io.KeyMap[ImGuiKey_V] = 'V';
  io.KeyMap[ImGuiKey_X] = 'X';
  io.KeyMap[ImGuiKey_Y] = 'Y';
  io.KeyMap[ImGuiKey_Z] = 'Z';
}

void ImGuiDrawer::SetupFont() {
  auto& io = GetIO();

  ImFontConfig font_config;
  font_config.OversampleH = font_config.OversampleV = 1;
  font_config.PixelSnapH = true;
  static const ImWchar font_glyph_ranges[] = {
      0x0020,
      0x00FF,  // Basic Latin + Latin Supplement
      0x0100,
      0x02AF,  // Latin Extended-A + Latin Extended-B + IPA Extensions
      0x0374,
      0x03FF,  // Basic Greek
      0x0400,
      0x04FF,  // Cyrillic
      0x1E00,
      0x1FFE,  // Latin Extended Additional + Greek Extended	  
      0,
  };
  io.Fonts->AddFontFromMemoryCompressedTTF(
      liberationmonoregular_compressed_data, liberationmonoregular_compressed_size, 12.0f, &font_config, font_glyph_ranges);

  ImFontConfig jp_font_config;
  jp_font_config.MergeMode = true;
  jp_font_config.OversampleH = jp_font_config.OversampleV = 1;
  jp_font_config.PixelSnapH = true;
  jp_font_config.FontNo = 0;
  io.Fonts->AddFontFromMemoryCompressedTTF(
        koruriregular_compressed_data, koruriregular_compressed_size, 12.0f, &jp_font_config, io.Fonts->GetGlyphRangesJapanese());

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  font_texture_ = graphics_context_->immediate_drawer()->CreateTexture(
      width, height, ImmediateTextureFilter::kLinear, true,
      reinterpret_cast<uint8_t*>(pixels));

  io.Fonts->TexID = reinterpret_cast<void*>(font_texture_->handle);
}

void ImGuiDrawer::RenderDrawLists(ImDrawData* data) {
  auto drawer = graphics_context_->immediate_drawer();

  // Handle cases of screen coordinates != from framebuffer coordinates (e.g.
  // retina displays).
  ImGuiIO& io = ImGui::GetIO();
  float fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;
  data->ScaleClipRects(io.DisplayFramebufferScale);

  const float width = io.DisplaySize.x;
  const float height = io.DisplaySize.y;
  drawer->Begin(static_cast<int>(width), static_cast<int>(height));

  for (int i = 0; i < data->CmdListsCount; ++i) {
    const auto cmd_list = data->CmdLists[i];

    ImmediateDrawBatch batch;
    batch.vertices =
        reinterpret_cast<ImmediateVertex*>(cmd_list->VtxBuffer.Data);
    batch.vertex_count = cmd_list->VtxBuffer.size();
    batch.indices = cmd_list->IdxBuffer.Data;
    batch.index_count = cmd_list->IdxBuffer.size();
    drawer->BeginDrawBatch(batch);

    int index_offset = 0;
    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto& cmd = cmd_list->CmdBuffer[j];

      ImmediateDraw draw;
      draw.primitive_type = ImmediatePrimitiveType::kTriangles;
      draw.count = cmd.ElemCount;
      draw.index_offset = index_offset;
      draw.texture_handle =
          reinterpret_cast<uintptr_t>(cmd.TextureId) & ~kIgnoreAlpha;
      draw.alpha_blend =
          reinterpret_cast<uintptr_t>(cmd.TextureId) & kIgnoreAlpha ? false
                                                                    : true;
      draw.scissor = true;
      draw.scissor_rect[0] = static_cast<int>(cmd.ClipRect.x);
      draw.scissor_rect[1] = static_cast<int>(height - cmd.ClipRect.w);
      draw.scissor_rect[2] = static_cast<int>(cmd.ClipRect.z - cmd.ClipRect.x);
      draw.scissor_rect[3] = static_cast<int>(cmd.ClipRect.w - cmd.ClipRect.y);
      drawer->Draw(draw);

      index_offset += cmd.ElemCount;
    }

    drawer->EndDrawBatch();
  }

  drawer->End();
}

ImGuiIO& ImGuiDrawer::GetIO() {
  ImGui::SetCurrentContext(internal_state_);
  return ImGui::GetIO();
}

void ImGuiDrawer::RenderDrawLists() {
  ImGui::SetCurrentContext(internal_state_);
  auto draw_data = ImGui::GetDrawData();
  if (draw_data) {
    RenderDrawLists(draw_data);
  }
}

void ImGuiDrawer::OnKeyDown(KeyEvent* e) {
  auto& io = GetIO();
  io.KeysDown[e->key_code()] = true;
  switch (e->key_code()) {
    case 16: {
      io.KeyShift = true;
    } break;
    case 17: {
      io.KeyCtrl = true;
    } break;
  }
}

void ImGuiDrawer::OnKeyUp(KeyEvent* e) {
  auto& io = GetIO();
  io.KeysDown[e->key_code()] = false;
  switch (e->key_code()) {
    case 16: {
      io.KeyShift = false;
    } break;
    case 17: {
      io.KeyCtrl = false;
    } break;
  }
}

void ImGuiDrawer::OnKeyChar(KeyEvent* e) {
  auto& io = GetIO();
  if (e->key_code() > 0 && e->key_code() < 0x10000) {
    io.AddInputCharacter(e->key_code());
    e->set_handled(true);
  }
}

void ImGuiDrawer::OnMouseDown(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  switch (e->button()) {
    case xe::ui::MouseEvent::Button::kLeft: {
      io.MouseDown[0] = true;
    } break;
    case xe::ui::MouseEvent::Button::kRight: {
      io.MouseDown[1] = true;
    } break;
    default: {
      // Ignored.
    } break;
  }
}

void ImGuiDrawer::OnMouseMove(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
}

void ImGuiDrawer::OnMouseUp(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  switch (e->button()) {
    case xe::ui::MouseEvent::Button::kLeft: {
      io.MouseDown[0] = false;
    } break;
    case xe::ui::MouseEvent::Button::kRight: {
      io.MouseDown[1] = false;
    } break;
    default: {
      // Ignored.
    } break;
  }
}

void ImGuiDrawer::OnMouseWheel(MouseEvent* e) {
  auto& io = GetIO();
  io.MousePos = ImVec2(float(e->x()), float(e->y()));
  io.MouseWheel += float(e->dy() / 120.0f);
}

}  // namespace ui
}  // namespace xe
