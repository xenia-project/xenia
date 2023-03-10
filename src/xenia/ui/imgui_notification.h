/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_NOTIFICATION_H_
#define XENIA_UI_IMGUI_NOTIFICATION_H_

#include "third_party/imgui/imgui.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

#define NOTIFY_TOAST_FLAGS                                            \
  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | \
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |            \
      ImGuiWindowFlags_NoBringToFrontOnFocus |                        \
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize

// Parameters based on 1280x720 resolution
constexpr ImVec2 default_drawing_resolution = ImVec2(1280.f, 720.f);

constexpr ImVec2 default_notification_icon_size = ImVec2(58.0f, 58.0f);
constexpr ImVec2 default_notification_margin_size = ImVec2(50.f, 5.f);
constexpr float default_notification_text_scale = 2.3f;
constexpr float default_notification_rounding = 30.f;
constexpr float default_font_size = 12.f;

constexpr ImVec4 white_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

constexpr ImVec4 default_notification_background_color =
    ImVec4(0.215f, 0.215f, 0.215f, 1.0f);
constexpr ImVec4 default_notification_border_color = white_color;

enum class NotificationAlignment : uint8_t {
  kAlignLeft = 0,
  kAlignRight = 1,
  kAlignCenter = 2,
  kAlignUnknown = 0xFF
};

const static std::vector<ImVec2> notification_position_id_screen_offset = {
    {0.50f, 0.45f},  // CENTER-CENTER - 0
    {0.50f, 0.10f},  // CENTER-TOP - 1
    {0.50f, 0.80f},  // CENTER-BOTTOM - 2
    {NAN, NAN},      // NOT EXIST - 3
    {0.07f, 0.45f},  // LEFT-CENTER - 4
    {0.07f, 0.10f},  // LEFT-TOP - 5
    {0.07f, 0.80f},  // LEFT-BOTTOM - 6
    {NAN, NAN},      // NOT EXIST - 7
    {0.93f, 0.45f},  // RIGHT-CENTER - 8
    {0.93f, 0.10f},  // RIGHT-TOP - 9
    {0.93f, 0.80f}   // RIGHT-BOTTOM - 10
};

namespace xe {
namespace ui {
class ImGuiNotification {
 public:
  ImGuiNotification(ui::ImGuiDrawer* imgui_drawer, std::string title,
                    std::string description, uint8_t user_index,
                    uint8_t position_id = 0);

  ~ImGuiNotification();

  void Draw();

 protected:
  enum class NotificationStage : uint8_t {
    kAwaiting = 0,
    kFazeIn = 1,
    kPresent = 2,
    kFazeOut = 3,
    kFinished = 4
  };

  ImGuiDrawer* GetDrawer() { return imgui_drawer_; }

  const bool IsNotificationClosingTime() {
    return Clock::QueryHostUptimeMillis() - creation_time_ > time_to_close_;
  }

  const bool IsNotificationExpired() {
    return current_stage_ == NotificationStage::kFinished;
  }

  const std::string GetNotificationText() {
    std::string text = title_;

    if (!description_.empty()) {
      text.append("\n" + description_);
    }
    return text;
  }

  const std::string GetTitle() { return title_; }
  const std::string GetDescription() { return description_; }

  const uint8_t GetPositionId() { return position_; }
  const uint8_t GetUserIndex() { return user_index_; }

  void UpdateNotificationState();

  virtual void OnDraw(ImGuiIO& io) {}

  float notification_draw_progress_;

 private:
  NotificationStage current_stage_;
  uint8_t position_;
  uint8_t user_index_;

  uint32_t delay_ = 0;
  uint32_t time_to_close_ = 4500;

  uint64_t creation_time_;

  std::string title_;
  std::string description_;

  ImGuiDrawer* imgui_drawer_ = nullptr;
};

class AchievementNotificationWindow final : ImGuiNotification {
 public:
  AchievementNotificationWindow(ui::ImGuiDrawer* imgui_drawer,
                                std::string title, std::string description,
                                uint8_t user_index, uint8_t position_id = 0)
      : ImGuiNotification(imgui_drawer, title, description, user_index,
                          position_id){};

  void OnDraw(ImGuiIO& io) override;
};

}  // namespace ui
}  // namespace xe

#endif