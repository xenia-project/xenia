/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
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
#include "xenia/ui/presenter.h"
#include "xenia/ui/ui_event.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_demo.h"

namespace xe {
namespace ui {

WindowDemoApp::~WindowDemoApp() { Profiler::Shutdown(); }

bool WindowDemoApp::OnInitialize() {
  Profiler::Initialize();
  Profiler::ThreadEnter("Main");

  // Create the graphics provider that provides the presenter for the window.
  graphics_provider_ = CreateGraphicsProvider();
  if (!graphics_provider_) {
    XELOGE("Failed to initialize the graphics provider");
    return false;
  }

  enum : size_t {
    kZOrderImGui,
    kZOrderProfiler,
    kZOrderWindowDemoInput,
  };

  // Create the window.
  window_ = xe::ui::Window::Create(app_context(), GetName(), 1920, 1200);
  window_->AddListener(&window_listener_);
  window_->AddInputListener(&window_listener_, kZOrderWindowDemoInput);

  // Main menu.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, "&File");
  {
    file_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Close", "Alt+F4",
                         [this]() { window_->RequestClose(); }));
  }
  main_menu->AddChild(std::move(file_menu));
  auto debug_menu = MenuItem::Create(MenuItem::Type::kPopup, "&Debug");
  {
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          "Toggle Profiler &Display", "Cmd+G",
                                          []() { Profiler::ToggleDisplay(); }));
    debug_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                          "&Pause/Resume Profiler", "Cmd+B",
                                          []() { Profiler::TogglePause(); }));
  }
  main_menu->AddChild(std::move(debug_menu));
  window_->SetMainMenu(std::move(main_menu));

  // Open the window once it's configured.
  if (!window_->Open()) {
    XELOGE("Failed to open the main window");
    return false;
  }

  // Setup drawing to the window.

  presenter_ = graphics_provider_->CreatePresenter();
  if (!presenter_) {
    XELOGE("Failed to initialize the presenter");
    return false;
  }

  immediate_drawer_ = graphics_provider_->CreateImmediateDrawer();
  if (!immediate_drawer_) {
    XELOGE("Failed to initialize the immediate drawer");
    return false;
  }
  immediate_drawer_->SetPresenter(presenter_.get());

  imgui_drawer_ = std::make_unique<ImGuiDrawer>(window_.get(), kZOrderImGui);
  imgui_drawer_->SetPresenterAndImmediateDrawer(presenter_.get(),
                                                immediate_drawer_.get());
  demo_dialog_ = std::make_unique<WindowDemoDialog>(imgui_drawer_.get());

  Profiler::SetUserIO(kZOrderProfiler, window_.get(), presenter_.get(),
                      immediate_drawer_.get());

  window_->SetPresenter(presenter_.get());

  return true;
}

void WindowDemoApp::WindowDemoWindowListener::OnClosing(UIEvent& e) {
  app_context_.QuitFromUIThread();
}

void WindowDemoApp::WindowDemoWindowListener::OnKeyDown(KeyEvent& e) {
  // Handle Command+G and Command+B shortcuts
  if (e.is_super_pressed()) {
    switch (e.virtual_key()) {
      case VirtualKey::kG:
        Profiler::ToggleDisplay();
        e.set_handled(true);
        return;
      case VirtualKey::kB:
        XELOGI("WindowDemo: Command+B pressed - toggling profiler pause");
        Profiler::TogglePause();
        e.set_handled(true);
        return;
    }
  }
  XELOGI("WindowDemo: Key pressed - virtual_key: {}, super_pressed: {}", 
         static_cast<int>(e.virtual_key()), e.is_super_pressed());
}

void WindowDemoApp::WindowDemoDialog::OnDraw(ImGuiIO& io) {
  // Force a very visible test window with solid colors
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red background
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White text
  ImGui::Begin("BIG VISIBLE TEST WINDOW", nullptr, 
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  
  ImGui::SetWindowSize(ImVec2(400, 300));
  ImGui::SetWindowPos(ImVec2(100, 100));
  
  // Large text that should be very visible
  ImGui::Text("===== HELLO FROM IMGUI! =====");
  ImGui::Text("This is a test window with red background");
  ImGui::Text("Mouse position: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
  ImGui::Text("Display size: %.1f x %.1f", io.DisplaySize.x, io.DisplaySize.y);
  
  if (ImGui::Button("BIG TEST BUTTON")) {
    // Button clicked
  }
  
  ImGui::End();
  ImGui::PopStyleColor(2);
  
  // Also show the demo windows
  ImGui::ShowDemoWindow();
  ImGui::ShowMetricsWindow();
}

}  // namespace ui
}  // namespace xe
