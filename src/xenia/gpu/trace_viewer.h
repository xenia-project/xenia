/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_VIEWER_H_
#define XENIA_GPU_TRACE_VIEWER_H_

#include <string_view>

#include "xenia/base/cvar.h"
#include "xenia/emulator.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/trace_player.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app.h"
DECLARE_path(target_trace_file);
namespace xe {
namespace gpu {

struct SamplerInfo;
struct TextureInfo;

class TraceViewer : public xe::ui::WindowedApp {
 public:
  virtual ~TraceViewer();

  bool OnInitialize() override;

 protected:
  explicit TraceViewer(xe::ui::WindowedAppContext& app_context,
                       const std::string_view name);

  virtual std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() = 0;
  GraphicsSystem* graphics_system() const { return graphics_system_; }

  void DrawMultilineString(const std::string_view str);

  virtual uintptr_t GetColorRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::ColorRenderTargetFormat format) = 0;
  virtual uintptr_t GetDepthRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::DepthRenderTargetFormat format) = 0;
  virtual uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                                    const SamplerInfo& sampler_info) = 0;

  virtual size_t QueryVSOutputSize() { return 0; }
  virtual size_t QueryVSOutputElementSize() { return 0; }
  virtual bool QueryVSOutput(void* buffer, size_t size) { return false; }

  virtual bool Setup();

 private:
  class TraceViewerWindowListener final : public xe::ui::WindowListener,
                                          public xe::ui::WindowInputListener {
   public:
    explicit TraceViewerWindowListener(TraceViewer& trace_viewer)
        : trace_viewer_(trace_viewer) {}

    void OnClosing(xe::ui::UIEvent& e) override;

    void OnKeyDown(xe::ui::KeyEvent& e) override;

   private:
    TraceViewer& trace_viewer_;
  };

  class TraceViewerDialog final : public ui::ImGuiDialog {
   public:
    explicit TraceViewerDialog(xe::ui::ImGuiDrawer* imgui_drawer,
                               TraceViewer& trace_viewer)
        : xe::ui::ImGuiDialog(imgui_drawer), trace_viewer_(trace_viewer) {}

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    TraceViewer& trace_viewer_;
  };

  enum class ShaderDisplayType : int {
    kUcode,
    kTranslated,
    kHostDisasm,
  };

  // Same as for Dear ImGui tooltips. Windows are translucent as the controls
  // may take a pretty large fraction of the screen, especially on small
  // screens, so the image from the guest can be seen through them.
  static constexpr float kWindowBgAlpha = 0.6f;

  bool Load(const std::string_view trace_file_path);

  void DrawUI();
  void DrawControllerUI();
  void DrawPacketDisassemblerUI();
  int RecursiveDrawCommandBufferUI(const TraceReader::Frame* frame,
                                   TraceReader::CommandBuffer* buffer);
  void DrawCommandListUI();
  void DrawStateUI();

  ShaderDisplayType DrawShaderTypeUI();
  void DrawShaderUI(Shader* shader, ShaderDisplayType display_type);

  void DrawBlendMode(uint32_t src_blend, uint32_t dest_blend,
                     uint32_t blend_op);

  void DrawTextureInfo(const Shader::TextureBinding& texture_binding);
  void DrawFailedTextureInfo(const Shader::TextureBinding& texture_binding,
                             const char* message);

  void DrawVertexFetcher(Shader* shader,
                         const Shader::VertexBinding& vertex_binding,
                         const xenos::xe_gpu_vertex_fetch_t* fetch);

  TraceViewerWindowListener window_listener_;

  std::unique_ptr<xe::ui::Window> window_;

  std::unique_ptr<Emulator> emulator_;
  Memory* memory_ = nullptr;
  GraphicsSystem* graphics_system_ = nullptr;
  std::unique_ptr<TracePlayer> player_;

  std::unique_ptr<xe::ui::ImmediateDrawer> immediate_drawer_;
  std::unique_ptr<xe::ui::ImGuiDrawer> imgui_drawer_;
  std::unique_ptr<TraceViewerDialog> trace_viewer_dialog_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_VIEWER_H_
