/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <cstring>

#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/threading.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/gl/gl_provider.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"

// Available input drivers:
#include "xenia/hid/nop/nop_hid.h"
#if XE_PLATFORM_WIN32
#include "xenia/hid/winkey/winkey_hid.h"
#include "xenia/hid/xinput/xinput_hid.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(hid, "any", "Input system. Use: [any, nop, winkey, xinput]");

namespace xe {
namespace hid {

std::unique_ptr<xe::hid::InputSystem> input_system_;

std::vector<std::unique_ptr<hid::InputDriver>> CreateInputDrivers(
    ui::Window* window) {
  std::vector<std::unique_ptr<hid::InputDriver>> drivers;
  if (FLAGS_hid.compare("nop") == 0) {
    drivers.emplace_back(xe::hid::nop::Create(window));
#if XE_PLATFORM_WIN32
  } else if (FLAGS_hid.compare("winkey") == 0) {
    drivers.emplace_back(xe::hid::winkey::Create(window));
  } else if (FLAGS_hid.compare("xinput") == 0) {
    drivers.emplace_back(xe::hid::xinput::Create(window));
#endif  // XE_PLATFORM_WIN32
  } else {
#if XE_PLATFORM_WIN32
    auto xinput_driver = xe::hid::xinput::Create(window);
    if (xinput_driver) {
      drivers.emplace_back(std::move(xinput_driver));
    }
    auto winkey_driver = xe::hid::winkey::Create(window);
    if (winkey_driver) {
      drivers.emplace_back(std::move(winkey_driver));
    }
#endif  // XE_PLATFORM_WIN32
    if (drivers.empty()) {
      // Fallback to nop if none created.
      drivers.emplace_back(xe::hid::nop::Create(window));
    }
  }
  return drivers;
}

std::unique_ptr<xe::ui::GraphicsProvider> CreateDemoGraphicsProvider(
    xe::ui::Window* window) {
  return xe::ui::gl::GLProvider::Create(window);
}

void DrawInputStatus();

int hid_demo_main(const std::vector<std::wstring>& args) {
  // Create run loop and the window.
  auto loop = ui::Loop::Create();
  auto window = xe::ui::Window::Create(loop.get(), GetEntryInfo().name);
  loop->PostSynchronous([&window]() {
    xe::threading::set_name("Win32 Loop");
    if (!window->Initialize()) {
      FatalError("Failed to initialize main window");
      return;
    }
  });
  window->on_closed.AddListener([&loop](xe::ui::UIEvent* e) {
    loop->Quit();
    XELOGI("User-initiated death!");
    exit(1);
  });
  loop->on_quit.AddListener([&window](xe::ui::UIEvent* e) { window.reset(); });

  // Initial size setting, done here so that it knows the menu exists.
  window->Resize(600, 500);

  // Create the graphics context used for drawing and setup the window.
  std::unique_ptr<xe::ui::GraphicsProvider> graphics_provider;
  loop->PostSynchronous([&window, &graphics_provider]() {
    // Create context and give it to the window.
    // The window will finish initialization wtih the context (loading
    // resources, etc).
    graphics_provider = CreateDemoGraphicsProvider(window.get());
    window->set_context(graphics_provider->CreateContext(window.get()));

    // Initialize input system and all drivers.
    input_system_ = std::make_unique<xe::hid::InputSystem>(window.get());
    auto drivers = CreateInputDrivers(window.get());
    for (size_t i = 0; i < drivers.size(); ++i) {
      input_system_->AddDriver(std::move(drivers[i]));
    }

    window->Invalidate();
  });

  window->on_painting.AddListener([&](xe::ui::UIEvent* e) {
    auto& io = window->imgui_drawer()->GetIO();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(-1, 0));
    ImGui::Begin("main_window", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowPos(ImVec2(0, 0));
    ImGui::SetWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

    DrawInputStatus();

    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();

    // Continuous paint.
    window->Invalidate();
  });

  // Wait until we are exited.
  loop->AwaitQuit();

  input_system_.reset();

  loop->PostSynchronous([&graphics_provider]() { graphics_provider.reset(); });
  window.reset();
  loop.reset();
  return 0;
}

void DrawUserInputStatus(uint32_t user_index) {
  ImGui::Text("User %u:", user_index);

  X_INPUT_STATE state;
  if (input_system_->GetState(user_index, &state) != X_ERROR_SUCCESS) {
    ImGui::Text(" No controller detected");
    return;
  }

  ImGui::Text(" Packet Number: %u", static_cast<uint32_t>(state.packet_number));

  auto& gamepad = state.gamepad;
  ImGui::Text("       Buttons: [%c][%c][%c][%c]  [%s][%s]",
              gamepad.buttons & X_INPUT_GAMEPAD_A ? 'A' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_B ? 'B' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_X ? 'X' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_Y ? 'Y' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_START ? "start" : "     ",
              gamepad.buttons & X_INPUT_GAMEPAD_BACK ? "back" : "    ");
  ImGui::Text("         D-pad: [%c][%c][%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_UP ? 'U' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_DOWN ? 'D' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_LEFT ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_RIGHT ? 'R' : ' ');
  ImGui::Text("        Thumbs: [%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_LEFT_THUMB ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_RIGHT_THUMB ? 'R' : ' ');
  ImGui::Text("     Shoulders: [%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_LEFT_SHOULDER ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_RIGHT_SHOULDER ? 'R' : ' ');
  ImGui::Text("  Left Trigger: %3u",
              static_cast<uint16_t>(gamepad.left_trigger));
  ImGui::Text(" Right Trigger: %3u",
              static_cast<uint16_t>(gamepad.right_trigger));
  ImGui::Text("    Left Thumb: %6d, %6d",
              static_cast<int32_t>(gamepad.thumb_lx),
              static_cast<int32_t>(gamepad.thumb_ly));
  ImGui::Text("   Right Thumb: %6d, %6d",
              static_cast<int32_t>(gamepad.thumb_rx),
              static_cast<int32_t>(gamepad.thumb_ry));

  X_INPUT_VIBRATION vibration;
  vibration.left_motor_speed = static_cast<uint16_t>(
      float(static_cast<uint16_t>(gamepad.left_trigger)) / 255.0f * UINT16_MAX);
  vibration.right_motor_speed = static_cast<uint16_t>(
      float(static_cast<uint16_t>(gamepad.right_trigger)) / 255.0f *
      UINT16_MAX);
  input_system_->SetState(user_index, &vibration);

  ImGui::Text(" Motor Speeds: L %5u, R %5u",
              static_cast<uint32_t>(vibration.left_motor_speed),
              static_cast<uint32_t>(vibration.right_motor_speed));

  ImGui::Text(" ");
}

void DrawInputStatus() {
  ImGui::BeginChild("##input_status", ImVec2(0, 0), true);

  for (uint32_t user_index = 0; user_index < 4; ++user_index) {
    DrawUserInputStatus(user_index);
  }

  ImGui::EndChild();
}

}  // namespace hid
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-hid-demo", L"xenia-hid-demo",
                   xe::hid::hid_demo_main);
