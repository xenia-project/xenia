/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_DEBUG_WINDOW_H_
#define XENIA_DEBUG_UI_DEBUG_WINDOW_H_

#include <memory>
#include <vector>

#include "xenia/base/host_thread_context.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/debug_listener.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

namespace xe {
namespace debug {
namespace ui {

class DebugWindow : public cpu::DebugListener {
 public:
 virtual ~DebugWindow();

  static std::unique_ptr<DebugWindow> Create(
      Emulator* emulator, xe::ui::WindowedAppContext& app_context);

  Emulator* emulator() const { return emulator_; }
  xe::ui::WindowedAppContext& app_context() const { return app_context_; }
  xe::ui::Window* window() const { return window_.get(); }

  void OnFocus() override;
  void OnDetached() override;
  void OnExecutionPaused() override;
  void OnExecutionContinued() override;
  void OnExecutionEnded() override;
  void OnStepCompleted(cpu::ThreadDebugInfo* thread_info) override;
  void OnBreakpointHit(cpu::Breakpoint* breakpoint,
                       cpu::ThreadDebugInfo* thread_info) override;

 private:
  class DebugDialog final : public xe::ui::ImGuiDialog {
   public:
    explicit DebugDialog(xe::ui::ImGuiDrawer* imgui_drawer,
                         DebugWindow& debug_window)
        : xe::ui::ImGuiDialog(imgui_drawer), debug_window_(debug_window) {}

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    DebugWindow& debug_window_;
  };

  explicit DebugWindow(Emulator* emulator,
                       xe::ui::WindowedAppContext& app_context);
  bool Initialize();

  void DrawFrame(ImGuiIO& io);
  void DrawToolbar();
  void DrawFunctionsPane();
  void DrawSourcePane();
  void DrawGuestFunctionSource();
  void DrawMachineCodeSource(const uint8_t* ptr, size_t length);
  void DrawBreakpointGutterButton(bool has_breakpoint,
                                  cpu::Breakpoint::AddressType address_type,
                                  uint64_t address);
  void ScrollToSourceIfPcChanged();
  void DrawRegistersPane();
  bool DrawRegisterTextBox(int id, uint32_t* value);
  bool DrawRegisterTextBox(int id, uint64_t* value);
  bool DrawRegisterTextBox(int id, double* value);
  bool DrawRegisterTextBoxes(int id, float* value);
  void DrawThreadsPane();
  void DrawMemoryPane();
  void DrawBreakpointsPane();
  void DrawLogPane();

  void SelectThreadStackFrame(cpu::ThreadDebugInfo* thread_info,
                              size_t stack_frame_index, bool always_navigate);
  void NavigateToFunction(cpu::Function* function, uint32_t guest_pc = 0,
                          uint64_t host_pc = 0);
  // void NavigateToMemory(uint64_t address, uint64_t length = 0);
  // void ToggleLogThreadFocus(thread | nullptr);

  void UpdateCache();

  void CreateCodeBreakpoint(cpu::Breakpoint::AddressType address_type,
                            uint64_t address);
  void DeleteCodeBreakpoint(cpu::Breakpoint* breakpoint);
  cpu::Breakpoint* LookupBreakpointAtAddress(
      cpu::Breakpoint::AddressType address_type, uint64_t address);

  void Focus() const;

  Emulator* emulator_ = nullptr;
  cpu::Processor* processor_ = nullptr;
  xe::ui::WindowedAppContext& app_context_;
  std::unique_ptr<xe::ui::Window> window_;
  std::unique_ptr<xe::ui::Presenter> presenter_;
  std::unique_ptr<xe::ui::ImmediateDrawer> immediate_drawer_;
  std::unique_ptr<xe::ui::ImGuiDrawer> imgui_drawer_;
  std::unique_ptr<DebugDialog> debug_dialog_;

  uintptr_t capstone_handle_ = 0;

  // Cached debugger data, updated on every break before a frame is drawn.
  // Prefer putting stuff here that will be queried either each frame or
  // multiple times per frame to avoid expensive redundant work.
  struct ImDataCache {
    bool is_running = false;
    std::vector<kernel::object_ref<kernel::XModule>> modules;
    std::vector<cpu::ThreadDebugInfo*> thread_debug_infos;
  } cache_;

  enum class RegisterGroup {
    kGuestGeneral,
    kGuestFloat,
    kGuestVector,
    kHostGeneral,
    kHostVector,
  };

  // The current state of the UI. Use this to synchronize multiple parts of the
  // UI.
  struct ImState {
    static const int kRightPaneThreads = 0;
    static const int kRightPaneMemory = 1;
    int right_pane_tab = kRightPaneThreads;

    cpu::ThreadDebugInfo* thread_info = nullptr;
    size_t thread_stack_frame_index = 0;
    bool has_changed_thread = false;

    xe::cpu::Function* function = nullptr;
    uint64_t last_host_pc = 0;
    bool has_changed_pc = false;
    int source_display_mode = 3;

    RegisterGroup register_group = RegisterGroup::kGuestGeneral;
    bool register_input_hex = true;

    struct {
      char kernel_call_filter[64] = {0};
      std::vector<std::unique_ptr<cpu::Breakpoint>> all_breakpoints;
      std::unordered_map<uint32_t, cpu::Breakpoint*>
          code_breakpoints_by_guest_address;
      std::unordered_map<uintptr_t, cpu::Breakpoint*>
          code_breakpoints_by_host_address;
    } breakpoints;

    xe::kernel::XThread* isolated_log_thread = nullptr;
  } state_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_DEBUG_WINDOW_H_
