/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <cstring>

#include "third_party/elemental-forms/src/el/util/debug.h"
#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

// Implemented in one of the window_*_demo.cc files under a subdir.
std::unique_ptr<GraphicsContext> CreateDemoContext(Window* window);

std::unique_ptr<xe::ui::ImGuiDrawer> imgui_drawer_;

int window_demo_main(const std::vector<std::wstring>& args) {
  Profiler::Initialize();
  Profiler::ThreadEnter("main");

  // Create run loop and the window.
  auto loop = ui::Loop::Create();
  auto window = xe::ui::Window::Create(loop.get(), GetEntryInfo().name);
  loop->PostSynchronous([&window]() {
    xe::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");
    if (!window->Initialize()) {
      FatalError("Failed to initialize main window");
      return;
    }
  });
  window->on_closed.AddListener([&loop](xe::ui::UIEvent* e) {
    loop->Quit();
    XELOGI("User-initiated death!");
    imgui_drawer_.reset();
    exit(1);
  });
  loop->on_quit.AddListener([&window](xe::ui::UIEvent* e) { window.reset(); });

  // Main menu.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&File");
  {
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kString, L"&Close",
                                         L"Alt+F4",
                                         [&window]() { window->Close(); }));
  }
  main_menu->AddChild(std::move(file_menu));
  auto debug_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&Debug");
  {
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          L"Toggle Profiler &Display", L"F3",
                                          []() { Profiler::ToggleDisplay(); }));
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          L"&Pause/Resume Profiler", L"`",
                                          []() { Profiler::TogglePause(); }));
  }
  main_menu->AddChild(std::move(debug_menu));
  window->set_main_menu(std::move(main_menu));

  // Initial size setting, done here so that it knows the menu exists.
  window->Resize(1920, 1200);

  // Create the graphics context used for drawing and setup the window.
  loop->PostSynchronous([&window]() {
    // Create context and give it to the window.
    // The window will finish initialization wtih the context (loading
    // resources, etc).
    auto context = CreateDemoContext(window.get());
    window->set_context(std::move(context));

    // Setup the profiler display.
    GraphicsContextLock context_lock(window->context());
    Profiler::set_window(window.get());

    // Initialize the ImGui renderer we'll use.
    imgui_drawer_ = std::make_unique<xe::ui::ImGuiDrawer>(window.get());

    // Show the elemental-forms debug UI so we can see it working.
    el::util::ShowDebugInfoSettingsForm(window->root_element());
  });

  window->on_key_char.AddListener([](xe::ui::KeyEvent* e) {
    auto& io = ImGui::GetIO();
    if (e->key_code() > 0 && e->key_code() < 0x10000) {
      io.AddInputCharacter(e->key_code());
    }
    e->set_handled(true);
  });
  window->on_key_down.AddListener([](xe::ui::KeyEvent* e) {
    auto& io = ImGui::GetIO();
    io.KeysDown[e->key_code()] = true;
    switch (e->key_code()) {
      case 0x72: {  // F3
        Profiler::ToggleDisplay();
      } break;
      case 16: {
        io.KeyShift = true;
      } break;
      case 17: {
        io.KeyCtrl = true;
      } break;
    }
  });
  window->on_key_up.AddListener([](xe::ui::KeyEvent* e) {
    auto& io = ImGui::GetIO();
    io.KeysDown[e->key_code()] = false;
    switch (e->key_code()) {
      case 16: {
        io.KeyShift = false;
      } break;
      case 17: {
        io.KeyCtrl = false;
      } break;
    }
  });
  window->on_mouse_down.AddListener([](xe::ui::MouseEvent* e) {
    auto& io = ImGui::GetIO();
    io.MousePos = ImVec2(float(e->x()), float(e->y()));
    switch (e->button()) {
      case xe::ui::MouseEvent::Button::kLeft: {
        io.MouseDown[0] = true;
      } break;
      case xe::ui::MouseEvent::Button::kRight: {
        io.MouseDown[1] = true;
      } break;
    }
  });
  window->on_mouse_move.AddListener([](xe::ui::MouseEvent* e) {
    auto& io = ImGui::GetIO();
    io.MousePos = ImVec2(float(e->x()), float(e->y()));
  });
  window->on_mouse_up.AddListener([](xe::ui::MouseEvent* e) {
    auto& io = ImGui::GetIO();
    io.MousePos = ImVec2(float(e->x()), float(e->y()));
    switch (e->button()) {
      case xe::ui::MouseEvent::Button::kLeft: {
        io.MouseDown[0] = false;
      } break;
      case xe::ui::MouseEvent::Button::kRight: {
        io.MouseDown[1] = false;
      } break;
    }
  });
  window->on_mouse_wheel.AddListener([](xe::ui::MouseEvent* e) {
    auto& io = ImGui::GetIO();
    io.MousePos = ImVec2(float(e->x()), float(e->y()));
    io.MouseWheel += float(e->dy() / 120.0f);
  });

  window->on_painting.AddListener([&](xe::ui::UIEvent* e) {
    auto& io = ImGui::GetIO();
    auto current_tick_count = Clock::QueryHostTickCount();
    static uint64_t last_draw_tick_count = 0;
    io.DeltaTime = (current_tick_count - last_draw_tick_count) /
                   static_cast<float>(Clock::host_tick_frequency());
    last_draw_tick_count = current_tick_count;

    io.DisplaySize = ImVec2(static_cast<float>(window->width()),
                            static_cast<float>(window->height()));

    ImGui::NewFrame();

    ImGui::ShowTestWindow();
    ImGui::ShowMetricsWindow();

    ImGui::Render();

    // Continuous paint.
    window->Invalidate();
  });

  // Wait until we are exited.
  loop->AwaitQuit();

  imgui_drawer_.reset();

  window.reset();
  loop.reset();
  Profiler::Dump();
  Profiler::Shutdown();
  return 0;
}

}  // namespace ui
}  // namespace xe
