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

#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/threading.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/gl/gl_context.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"

namespace xe {
namespace hid {

std::unique_ptr<xe::ui::ImGuiDrawer> imgui_drawer_;
std::unique_ptr<xe::hid::InputSystem> input_system_;

std::unique_ptr<xe::ui::GraphicsContext> CreateDemoContext(
    xe::ui::Window* window) {
  return xe::ui::gl::GLContext::Create(window);
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
    imgui_drawer_.reset();
    exit(1);
  });
  loop->on_quit.AddListener([&window](xe::ui::UIEvent* e) { window.reset(); });

  // Initial size setting, done here so that it knows the menu exists.
  window->Resize(600, 500);

  // Create the graphics context used for drawing and setup the window.
  loop->PostSynchronous([&window]() {
    // Create context and give it to the window.
    // The window will finish initialization wtih the context (loading
    // resources, etc).
    auto context = CreateDemoContext(window.get());
    window->set_context(std::move(context));

    // Initialize the ImGui renderer we'll use.
    imgui_drawer_ = std::make_unique<xe::ui::ImGuiDrawer>(window.get());

    // Initialize input system and all drivers.
    input_system_ = xe::hid::InputSystem::Create(window.get());
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(-1, 0));
    ImGui::Begin("main_window", nullptr, ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoResize |
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

    ImGui::Render();

    // Continuous paint.
    window->Invalidate();
  });

  // Wait until we are exited.
  loop->AwaitQuit();

  imgui_drawer_.reset();
  input_system_.reset();

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
  ImGui::Text("  Left Trigger: %3u", gamepad.left_trigger);
  ImGui::Text(" Right Trigger: %3u", gamepad.right_trigger);
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
