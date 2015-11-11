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
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {

// Implemented in one of the window_*_demo.cc files under a subdir.
std::unique_ptr<GraphicsProvider> CreateDemoGraphicsProvider(Window* window);

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
  std::unique_ptr<GraphicsProvider> graphics_provider;
  std::unique_ptr<xe::ui::ImGuiDrawer> imgui_drawer;
  loop->PostSynchronous([&window, &graphics_provider, &imgui_drawer]() {
    // Create graphics provider and an initial context for the window.
    // The window will finish initialization wtih the context (loading
    // resources, etc).
    graphics_provider = CreateDemoGraphicsProvider(window.get());
    window->set_context(graphics_provider->CreateContext(window.get()));

    // Setup the profiler display.
    GraphicsContextLock context_lock(window->context());
    Profiler::set_window(window.get());

    // Initialize the ImGui renderer we'll use.
    imgui_drawer = std::make_unique<xe::ui::ImGuiDrawer>(window.get());
    imgui_drawer->SetupDefaultInput();

    // Show the elemental-forms debug UI so we can see it working.
    el::util::ShowDebugInfoSettingsForm(window->root_element());
  });

  window->on_closed.AddListener(
      [&loop, &graphics_provider, &imgui_drawer](xe::ui::UIEvent* e) {
        loop->Quit();
        XELOGI("User-initiated death!");
        imgui_drawer.reset();
        graphics_provider.reset();
        exit(1);
      });
  loop->on_quit.AddListener([&window](xe::ui::UIEvent* e) { window.reset(); });

  window->on_key_down.AddListener([](xe::ui::KeyEvent* e) {
    switch (e->key_code()) {
      case 0x72: {  // F3
        Profiler::ToggleDisplay();
      } break;
    }
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

  imgui_drawer.reset();

  loop->PostSynchronous([&graphics_provider]() { graphics_provider.reset(); });
  window.reset();
  loop.reset();
  Profiler::Dump();
  Profiler::Shutdown();
  return 0;
}

}  // namespace ui
}  // namespace xe
