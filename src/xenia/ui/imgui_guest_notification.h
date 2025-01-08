/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_GUEST_NOTIFICATION_H_
#define XENIA_UI_IMGUI_GUEST_NOTIFICATION_H_

#include "third_party/imgui/imgui.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_notification.h"

/*

*/

namespace xe {
namespace ui {
class ImGuiGuestNotification : public ImGuiNotification {
 public:
  ImGuiGuestNotification(ui::ImGuiDrawer* imgui_drawer, std::string& title,
                         std::string& description, uint8_t user_index,
                         uint8_t position_id = 0);

  ~ImGuiGuestNotification();

 protected:
  const ImVec2 default_notification_icon_size = ImVec2(58.0f, 58.0f);
  const ImVec2 default_notification_margin_size = ImVec2(50.f, 5.f);
  const float default_notification_text_scale = 2.3f;
  const float default_notification_rounding = 30.f;
  const float default_font_size = 12.f;

  const ImVec4 white_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

  const ImVec4 default_notification_background_color =
      ImVec4(0.215f, 0.215f, 0.215f, 1.0f);
  const ImVec4 default_notification_border_color = white_color;

  enum class NotificationStage : uint8_t {
    Awaiting = 0,
    FazeIn = 1,
    Present = 2,
    FazeOut = 3,
    Finished = 4
  };

  const bool IsNotificationExpired() {
    return current_stage_ == NotificationStage::Finished;
  }

  void UpdateNotificationState();
  const ImVec2 CalculateNotificationSize(ImVec2 text_size,
                                         float scale) override;

  virtual void OnDraw(ImGuiIO& io) override {}

  float notification_draw_progress_;

 private:
  NotificationStage current_stage_;
};

class AchievementNotificationWindow final : ImGuiGuestNotification {
 public:
  AchievementNotificationWindow(ui::ImGuiDrawer* imgui_drawer,
                                std::string title, std::string description,
                                uint8_t user_index, uint8_t position_id = 0)
      : ImGuiGuestNotification(imgui_drawer, title, description, user_index,
                               position_id) {};

  void OnDraw(ImGuiIO& io) override;
};

class XNotifyWindow final : ImGuiGuestNotification {
 public:
  XNotifyWindow(ui::ImGuiDrawer* imgui_drawer, std::string title,
                std::string description, uint8_t user_index,
                uint8_t position_id = 0)
      : ImGuiGuestNotification(imgui_drawer, title, description, user_index,
                               position_id) {};

  void OnDraw(ImGuiIO& io) override;
};

}  // namespace ui
}  // namespace xe

#endif