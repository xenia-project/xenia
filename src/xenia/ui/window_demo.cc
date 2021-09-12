/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_demo.h"

namespace xe {
namespace ui {

WindowDemoApp::~WindowDemoApp() { Profiler::Shutdown(); }

bool WindowDemoApp::OnInitialize() {
  Profiler::Initialize();
  Profiler::ThreadEnter("Main");

  // Create graphics provider that provides the context for the window.
  graphics_provider_ = CreateGraphicsProvider();
  if (!graphics_provider_) {
    return false;
  }

  // Create the window.
  window_ = xe::ui::Window::Create(app_context(), GetName());
  if (!window_->Initialize()) {
    XELOGE("Failed to initialize main window");
    return false;
  }

  // Main menu.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, "&File");
  {
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kString, "&Close",
                                         "Alt+F4",
                                         [this]() { window_->Close(); }));
  }
  main_menu->AddChild(std::move(file_menu));
  auto debug_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Debug");
  {
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          "Toggle Profiler &Display", "F3",
                                          []() { Profiler::ToggleDisplay(); }));
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          "&Pause/Resume Profiler", "`",
                                          []() { Profiler::TogglePause(); }));
  }
  main_menu->AddChild(std::move(debug_menu));
  window_->set_main_menu(std::move(main_menu));

  // Initial size setting, done here so that it knows the menu exists.
  window_->Resize(1920, 1200);

  // Create the graphics context for the window. The window will finish
  // initialization with the context (loading resources, etc).
  window_->set_context(graphics_provider_->CreateHostContext(window_.get()));

  // Setup the profiler display.
  GraphicsContextLock context_lock(window_->context());
  Profiler::set_window(window_.get());

  // Enable imgui input.
  window_->set_imgui_input_enabled(true);

  window_->on_closed.AddListener([this](xe::ui::UIEvent* e) {
    XELOGI("User-initiated death!");
    app_context().QuitFromUIThread();
  });

  window_->on_key_down.AddListener([](xe::ui::KeyEvent* e) {
    switch (e->virtual_key()) {
      case VirtualKey::kF3:
        Profiler::ToggleDisplay();
        break;
      default:
        break;
    }
  });

  window_->on_painting.AddListener([this](xe::ui::UIEvent* e) {
    auto& io = window_->imgui_drawer()->GetIO();

    ImGui::ShowDemoWindow();
    ImGui::ShowMetricsWindow();

    Profiler::Flip();

    // Continuous paint.
    window_->Invalidate();
  });

  return true;
}

}  // namespace ui
}  // namespace xe
