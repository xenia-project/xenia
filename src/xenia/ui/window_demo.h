/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_DEMO_H_
#define XENIA_UI_WINDOW_DEMO_H_

#include <memory>
#include <string>

#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

class WindowDemoApp : public WindowedApp {
 public:
  ~WindowDemoApp();

  bool OnInitialize() override;

 protected:
  explicit WindowDemoApp(WindowedAppContext& app_context,
                         const std::string_view name)
      : WindowedApp(app_context, name), window_listener_(app_context) {}

  virtual std::unique_ptr<GraphicsProvider> CreateGraphicsProvider() const = 0;

 private:
  class WindowDemoWindowListener final : public WindowListener,
                                         public WindowInputListener {
   public:
    explicit WindowDemoWindowListener(WindowedAppContext& app_context)
        : app_context_(app_context) {}

    void OnClosing(UIEvent& e) override;

    void OnKeyDown(KeyEvent& e) override;

   private:
    WindowedAppContext& app_context_;
  };

  class WindowDemoDialog final : public ImGuiDialog {
   public:
    explicit WindowDemoDialog(ImGuiDrawer* imgui_drawer)
        : ImGuiDialog(imgui_drawer) {}

   protected:
    void OnDraw(ImGuiIO& io) override;
  };

  WindowDemoWindowListener window_listener_;
  std::unique_ptr<GraphicsProvider> graphics_provider_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<Presenter> presenter_;
  std::unique_ptr<ImmediateDrawer> immediate_drawer_;
  std::unique_ptr<ImGuiDrawer> imgui_drawer_;
  std::unique_ptr<WindowDemoDialog> demo_dialog_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_DEMO_H_
