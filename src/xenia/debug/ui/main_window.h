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
#include "xenia/ui/elemental_control.h"
#include "xenia/ui/platform.h"
#include "xenia/ui/window.h"

namespace xe {
namespace debug {
namespace ui {

class MainWindow : public xe::ui::PlatformWindow {
 public:
  MainWindow(Application* app);
  ~MainWindow() override;

  Application* app() const { return app_; }

  bool Initialize();

  // void NavigateToCpuView(uint32_t address);

 private:
  void BuildUI();

  void OnClose() override;
  void OnCommand(int id) override;

  Application* app_ = nullptr;
  xe::ui::PlatformMenu main_menu_;
  std::unique_ptr<xe::ui::ElementalControl> control_;

  std::unique_ptr<el::Window> window_;
  struct {
    el::SplitContainer* split_container;
    el::LayoutBox* toolbar_box;
    el::TabContainer* tab_container;
  } ui_ = {0};
  views::cpu::CpuView cpu_view_;
  views::gpu::GpuView gpu_view_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_MAIN_WINDOW_H_
