/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <array>
#include <cstring>
#include <forward_list>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app.h"

// Available input drivers:
#include "xenia/hid/nop/nop_hid.h"
#if !XE_PLATFORM_ANDROID
#include "xenia/hid/sdl/sdl_hid.h"
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
#include "xenia/hid/winkey/winkey_hid.h"
#include "xenia/hid/xinput/xinput_hid.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(hid, "any", "Input system. Use: [any, nop, sdl, winkey, xinput]",
              "General");

#define MAX_USERS 4
#define ROW_HEIGHT_GENERAL 60
#define COL_WIDTH_STATE 320
#define COL_WIDTH_STROKE 416

namespace xe {
namespace hid {

class HidDemoApp final : public ui::WindowedApp {
 public:
  static std::unique_ptr<ui::WindowedApp> Create(
      ui::WindowedAppContext& app_context) {
    return std::unique_ptr<ui::WindowedApp>(new HidDemoApp(app_context));
  }

  bool OnInitialize() override;

 private:
  enum : size_t {
    kZOrderHidInput,
    kZOrderImGui,
  };

  class HidDemoWindowListener final : public ui::WindowListener {
   public:
    explicit HidDemoWindowListener(ui::WindowedAppContext& app_context)
        : app_context_(app_context) {}
    void OnClosing(ui::UIEvent& e) override { app_context_.QuitFromUIThread(); }

   private:
    ui::WindowedAppContext& app_context_;
  };

  class HidDemoDialog final : public ui::ImGuiDialog {
   public:
    explicit HidDemoDialog(ui::ImGuiDrawer* imgui_drawer, HidDemoApp& app)
        : ui::ImGuiDialog(imgui_drawer), app_(app) {}

   protected:
    void OnDraw(ImGuiIO& io) override;

   private:
    HidDemoApp& app_;
  };

  explicit HidDemoApp(ui::WindowedAppContext& app_context)
      : ui::WindowedApp(app_context, "xenia-hid-demo"),
        window_listener_(app_context) {}

  static std::vector<std::unique_ptr<hid::InputDriver>> CreateInputDrivers(
      ui::Window* window);

  void Draw(ImGuiIO& io);
  void DrawUserInputGetState(uint32_t user_index) const;
  void DrawInputGetState() const;
  void DrawUserInputGetKeystroke(uint32_t user_index, bool poll,
                                 bool hide_repeats, bool clear_log) const;
  void DrawInputGetKeystroke(bool poll, bool hide_repeats,
                             bool clear_log) const;

  HidDemoWindowListener window_listener_;
  std::unique_ptr<ui::GraphicsProvider> graphics_provider_;
  std::unique_ptr<ui::Window> window_;
  std::unique_ptr<InputSystem> input_system_;
  std::unique_ptr<ui::Presenter> presenter_;
  std::unique_ptr<ui::ImmediateDrawer> immediate_drawer_;
  std::unique_ptr<ui::ImGuiDrawer> imgui_drawer_;
  std::unique_ptr<HidDemoDialog> demo_dialog_;

  bool is_active_ = true;
};

std::vector<std::unique_ptr<hid::InputDriver>> HidDemoApp::CreateInputDrivers(
    ui::Window* window) {
  std::vector<std::unique_ptr<hid::InputDriver>> drivers;
  if (cvars::hid.compare("nop") == 0) {
    drivers.emplace_back(xe::hid::nop::Create(window, kZOrderHidInput));
#if !XE_PLATFORM_ANDROID
  } else if (cvars::hid.compare("sdl") == 0) {
    auto driver = xe::hid::sdl::Create(window, kZOrderHidInput);
    if (XSUCCEEDED(driver->Setup())) {
      drivers.emplace_back(std::move(driver));
    }
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
  } else if (cvars::hid.compare("winkey") == 0) {
    auto driver = xe::hid::winkey::Create(window, kZOrderHidInput);
    if (XSUCCEEDED(driver->Setup())) {
      drivers.emplace_back(std::move(driver));
    }
  } else if (cvars::hid.compare("xinput") == 0) {
    auto driver = xe::hid::xinput::Create(window, kZOrderHidInput);
    if (XSUCCEEDED(driver->Setup())) {
      drivers.emplace_back(std::move(driver));
    }
#endif  // XE_PLATFORM_WIN32
  } else {
#if !XE_PLATFORM_ANDROID
    auto sdl_driver = xe::hid::sdl::Create(window, kZOrderHidInput);
    if (sdl_driver && XSUCCEEDED(sdl_driver->Setup())) {
      drivers.emplace_back(std::move(sdl_driver));
    }
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
    auto xinput_driver = xe::hid::xinput::Create(window, kZOrderHidInput);
    if (xinput_driver && XSUCCEEDED(xinput_driver->Setup())) {
      drivers.emplace_back(std::move(xinput_driver));
    }
    auto winkey_driver = xe::hid::winkey::Create(window, kZOrderHidInput);
    if (winkey_driver && XSUCCEEDED(winkey_driver->Setup())) {
      drivers.emplace_back(std::move(winkey_driver));
    }
#endif  // XE_PLATFORM_WIN32
    if (drivers.empty()) {
      // Fallback to nop if none created.
      drivers.emplace_back(xe::hid::nop::Create(window, kZOrderHidInput));
    }
  }
  return drivers;
}

bool HidDemoApp::OnInitialize() {
  // Create the graphics provider that provides the presenter for the window.
  graphics_provider_ = xe::ui::vulkan::VulkanProvider::Create(true);
  if (!graphics_provider_) {
    XELOGE("Failed to initialize the graphics provider");
    return false;
  }

  // Create and configure the window.
  window_ = xe::ui::Window::Create(app_context(), GetName(),
                                   COL_WIDTH_STATE + COL_WIDTH_STROKE,
                                   ROW_HEIGHT_GENERAL + 500);
  window_->AddListener(&window_listener_);
  if (!window_->Open()) {
    XELOGE("Failed to open the main window");
    return false;
  }

  // Initialize input system and all drivers.
  input_system_ = std::make_unique<xe::hid::InputSystem>(window_.get());
  auto drivers = CreateInputDrivers(window_.get());
  for (size_t i = 0; i < drivers.size(); ++i) {
    auto& driver = drivers[i];
    driver->set_is_active_callback([this]() -> bool { return is_active_; });
    input_system_->AddDriver(std::move(driver));
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
  imgui_drawer_ =
      std::make_unique<ui::ImGuiDrawer>(window_.get(), kZOrderImGui);
  imgui_drawer_->SetPresenterAndImmediateDrawer(presenter_.get(),
                                                immediate_drawer_.get());
  demo_dialog_ = std::make_unique<HidDemoDialog>(imgui_drawer_.get(), *this);
  window_->SetPresenter(presenter_.get());

  return true;
}

void HidDemoApp::HidDemoDialog::OnDraw(ImGuiIO& io) { app_.Draw(io); }

void HidDemoApp::Draw(ImGuiIO& io) {
  const ImGuiWindowFlags wflags =
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoScrollbar;

  ImGui::Begin("General", nullptr, wflags);
  {
    ImGui::SetWindowPos(ImVec2(0, 0));
    ImGui::SetWindowSize(
        ImVec2(COL_WIDTH_STATE + COL_WIDTH_STROKE, ROW_HEIGHT_GENERAL));

    ImGui::Text("Input System (hid) = \"%s\"", cvars::hid.c_str());
    ImGui::Checkbox("is_active", &is_active_);
  }
  ImGui::End();

  ImGui::Begin("GetState()", nullptr, wflags);
  {
    ImGui::SetWindowPos(ImVec2(0, ROW_HEIGHT_GENERAL));
    ImGui::SetWindowSize(
        ImVec2(COL_WIDTH_STATE, io.DisplaySize.y - ROW_HEIGHT_GENERAL));

    static bool enable_GetState = false;
    ImGui::Checkbox("Active", &enable_GetState);
    ImGui::SameLine();
    ImGui::Checkbox("Guide Button", &cvars::guide_button);
    if (enable_GetState) {
      ImGui::Spacing();
      DrawInputGetState();
    }
  }
  ImGui::End();

  ImGui::Begin("GetKeystroke()", nullptr, wflags);
  {
    ImGui::SetWindowPos(ImVec2(COL_WIDTH_STATE, ROW_HEIGHT_GENERAL));
    ImGui::SetWindowSize(
        ImVec2(COL_WIDTH_STROKE, io.DisplaySize.y - ROW_HEIGHT_GENERAL));

    static bool enable_GetKeystroke = false;
    static bool hide_repeats = false;
    ImGui::Checkbox("Active", &enable_GetKeystroke);
    ImGui::SameLine();
    ImGui::Checkbox("Hide repeats", &hide_repeats);
    ImGui::SameLine();
    const bool clear_log = ImGui::Button("Clear");
    ImGui::Spacing();
    DrawInputGetKeystroke(enable_GetKeystroke, hide_repeats, clear_log);
  }
  ImGui::End();
}

void HidDemoApp::DrawUserInputGetState(uint32_t user_index) const {
  ImGui::Text("User %u:", user_index);

  X_INPUT_STATE state;
  if (input_system_->GetState(user_index, &state) != X_ERROR_SUCCESS) {
    ImGui::Text(" No controller detected");
    return;
  }

  ImGui::Text("  Packet Number: %u",
              static_cast<uint32_t>(state.packet_number));

  auto& gamepad = state.gamepad;
  ImGui::Text("  Right Buttons: [%c][%c][%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_A ? 'A' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_B ? 'B' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_X ? 'X' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_Y ? 'Y' : ' ');
  ImGui::Text("Special Buttons: [%s][%s][%s]",
              gamepad.buttons & X_INPUT_GAMEPAD_BACK ? "back" : "    ",
              gamepad.buttons & X_INPUT_GAMEPAD_GUIDE ? "guide" : "     ",
              gamepad.buttons & X_INPUT_GAMEPAD_START ? "start" : "     ");
  ImGui::Text("          D-pad: [%c][%c][%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_UP ? 'U' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_DOWN ? 'D' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_LEFT ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_DPAD_RIGHT ? 'R' : ' ');
  ImGui::Text("         Thumbs: [%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_LEFT_THUMB ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_RIGHT_THUMB ? 'R' : ' ');
  ImGui::Text("      Shoulders: [%c][%c]",
              gamepad.buttons & X_INPUT_GAMEPAD_LEFT_SHOULDER ? 'L' : ' ',
              gamepad.buttons & X_INPUT_GAMEPAD_RIGHT_SHOULDER ? 'R' : ' ');
  ImGui::Text("   Left Trigger: %3u",
              static_cast<uint16_t>(gamepad.left_trigger));
  ImGui::Text("  Right Trigger: %3u",
              static_cast<uint16_t>(gamepad.right_trigger));
  ImGui::Text("     Left Thumb: %6d, %6d",
              static_cast<int32_t>(gamepad.thumb_lx),
              static_cast<int32_t>(gamepad.thumb_ly));
  ImGui::Text("    Right Thumb: %6d, %6d",
              static_cast<int32_t>(gamepad.thumb_rx),
              static_cast<int32_t>(gamepad.thumb_ry));

  X_INPUT_VIBRATION vibration;
  vibration.left_motor_speed = static_cast<uint16_t>(
      float(static_cast<uint16_t>(gamepad.left_trigger)) / 255.0f * UINT16_MAX);
  vibration.right_motor_speed = static_cast<uint16_t>(
      float(static_cast<uint16_t>(gamepad.right_trigger)) / 255.0f *
      UINT16_MAX);
  input_system_->SetState(user_index, &vibration);

  ImGui::Text("  Motor Speeds: L %5u, R %5u",
              static_cast<uint32_t>(vibration.left_motor_speed),
              static_cast<uint32_t>(vibration.right_motor_speed));

  ImGui::Text(" ");
}

void HidDemoApp::DrawInputGetState() const {
  ImGui::BeginChild("##input_get_state_scroll");
  for (uint32_t user_index = 0; user_index < MAX_USERS; ++user_index) {
    DrawUserInputGetState(user_index);
  }
  ImGui::EndChild();
}

void HidDemoApp::DrawUserInputGetKeystroke(uint32_t user_index, bool poll,
                                           bool hide_repeats,
                                           bool clear_log) const {
  static const std::unordered_map<ui::VirtualKey, const std::string> kVkPretty =
      {
          {ui::VirtualKey::kXInputPadGuide, "Guide"},
          {ui::VirtualKey::kXInputPadA, "A"},
          {ui::VirtualKey::kXInputPadB, "B"},
          {ui::VirtualKey::kXInputPadX, "X"},
          {ui::VirtualKey::kXInputPadY, "Y"},
          {ui::VirtualKey::kXInputPadRShoulder, "R Shoulder"},
          {ui::VirtualKey::kXInputPadLShoulder, "L Shoulder"},
          {ui::VirtualKey::kXInputPadLTrigger, "L Trigger"},
          {ui::VirtualKey::kXInputPadRTrigger, "R Trigger"},

          {ui::VirtualKey::kXInputPadDpadUp, "DPad up"},
          {ui::VirtualKey::kXInputPadDpadDown, "DPad down"},
          {ui::VirtualKey::kXInputPadDpadLeft, "DPad left"},
          {ui::VirtualKey::kXInputPadDpadRight, "DPad right"},
          {ui::VirtualKey::kXInputPadStart, "Start"},
          {ui::VirtualKey::kXInputPadBack, "Back"},
          {ui::VirtualKey::kXInputPadLThumbPress, "L Thumb press"},
          {ui::VirtualKey::kXInputPadRThumbPress, "R Thumb press"},

          {ui::VirtualKey::kXInputPadLThumbUp, "L Thumb up"},
          {ui::VirtualKey::kXInputPadLThumbDown, "L Thumb down"},
          {ui::VirtualKey::kXInputPadLThumbRight, "L Thumb right"},
          {ui::VirtualKey::kXInputPadLThumbLeft, "L Thumb left"},
          {ui::VirtualKey::kXInputPadLThumbUpLeft, "L Thumb up & left"},
          {ui::VirtualKey::kXInputPadLThumbUpRight, "L Thumb up & right"},
          {ui::VirtualKey::kXInputPadLThumbDownRight, "L Thumb down & right"},
          {ui::VirtualKey::kXInputPadLThumbDownLeft, "L Thumb down & left"},

          {ui::VirtualKey::kXInputPadRThumbUp, "R Thumb up"},
          {ui::VirtualKey::kXInputPadRThumbDown, "R Thumb down"},
          {ui::VirtualKey::kXInputPadRThumbRight, "R Thumb right"},
          {ui::VirtualKey::kXInputPadRThumbLeft, "R Thumb left"},
          {ui::VirtualKey::kXInputPadRThumbUpLeft, "R Thumb up & left"},
          {ui::VirtualKey::kXInputPadRThumbUpRight, "R Thumb up & right"},
          {ui::VirtualKey::kXInputPadRThumbDownRight, "R Thumb down & right"},
          {ui::VirtualKey::kXInputPadRThumbDownLeft, "R Thumb down & left"},
      };

  const size_t maxLog = 128;
  static std::array<std::forward_list<std::string>, MAX_USERS> event_logs;
  static std::array<uint64_t, MAX_USERS> last_event_times = {};

  auto& event_log = event_logs.at(user_index);
  if (clear_log) {
    event_log.clear();
  }

  if (poll) {
    X_INPUT_KEYSTROKE stroke;
    bool continue_poll;
    do {
      continue_poll = false;
      auto status = input_system_->GetKeystroke(user_index, 0, &stroke);
      switch (status) {
        case X_ERROR_SUCCESS: {
          if (!stroke.virtual_key) {
            event_log.emplace_front("Error: Empty KEYSTROKE on SUCCESS.");
            break;
          }
          continue_poll = true;

          const auto now = Clock::QueryHostUptimeMillis();
          const auto dur = now - last_event_times[user_index];
          last_event_times[user_index] = now;

          if (hide_repeats && (stroke.flags & X_INPUT_KEYSTROKE_REPEAT)) {
            break;
          }

          ui::VirtualKey virtual_key = ui::VirtualKey(stroke.virtual_key.get());
          const auto key_search = kVkPretty.find(virtual_key);
          std::string key =
              key_search != kVkPretty.cend()
                  ? key_search->second
                  : fmt::format("0x{:04x}", uint16_t(virtual_key));
          event_log.emplace_front(fmt::format(
              "{:>6} {:>9}ms    {:<20}    {} {} {}", ImGui::GetFrameCount(),
              dur, key,
              ((stroke.flags & X_INPUT_KEYSTROKE_KEYDOWN) ? "down" : "    "),
              ((stroke.flags & X_INPUT_KEYSTROKE_KEYUP) ? "up" : "  "),
              ((stroke.flags & X_INPUT_KEYSTROKE_REPEAT) ? "repeat" : "")));

          break;
        }
        case X_ERROR_DEVICE_NOT_CONNECTED:
        case X_ERROR_EMPTY:
          break;
        default:
          event_log.emplace_front(
              fmt::format("Error: Unknown result code: 0x{:08X}", status));
          break;
      }
    } while (continue_poll);
  }

  if (ImGui::BeginTabItem(fmt::format("User {}", user_index).c_str())) {
    ImGui::BeginChild(
        fmt::format("##input_get_keystroke_scroll_{}", user_index).c_str());
    ImGui::Text(" frame  since last    button                  flags");
    for (auto [it, count] = std::tuple{event_log.begin(), 1};
         it != event_log.end(); ++it, ++count) {
      ImGui::Text("%s", it->c_str());

      if (count >= maxLog) {
        auto last = it;
        if (++it != event_log.end()) {
          event_log.erase_after(last);
        }
        break;
      }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
}

void HidDemoApp::DrawInputGetKeystroke(bool poll, bool hide_repeats,
                                       bool clear_log) const {
  bool tab_bar = ImGui::BeginTabBar("DrawInputGetKeystroke");
  for (uint32_t user_index = 0; user_index < MAX_USERS; ++user_index) {
    DrawUserInputGetKeystroke(user_index, poll, hide_repeats, clear_log);
  }
  if (tab_bar) ImGui::EndTabBar();
}

}  // namespace hid
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_hid_demo, xe::hid::HidDemoApp::Create);
