/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_MAIN_WINDOW_H_
#define XENIA_DEBUG_UI_MAIN_WINDOW_H_

#include <memory>

#include "el/elements.h"
#include "xenia/debug/ui/application.h"
#include "xenia/debug/ui/views/cpu/cpu_view.h"
#include "xenia/debug/ui/views/gpu/gpu_view.h"
#include "xenia/ui/window.h"

namespace xe {
namespace debug {
namespace ui {

class MainWindow {
 public:
  MainWindow(Application* app);
  ~MainWindow();

  Application* app() const { return app_; }
  xe::ui::Loop* loop() const { return app_->loop(); }
  model::System* system() const { return app_->system(); }

  bool Initialize();

  // void NavigateToCpuView(uint32_t address);

 private:
  void BuildUI();
  void UpdateElementState();

  void OnClose();

  Application* app_ = nullptr;
  xe::debug::client::xdp::XdpClient* client_ = nullptr;

  std::unique_ptr<xe::ui::Window> window_;
  std::unique_ptr<el::Form> form_;
  struct {
    el::SplitContainer* split_container;
    el::LayoutBox* toolbar_box;
    el::TabContainer* tab_container;
  } ui_ = {0};
  std::unique_ptr<el::EventHandler> handler_;
  views::cpu::CpuView cpu_view_;
  views::gpu::GpuView gpu_view_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MAIN_WINDOW_H_
