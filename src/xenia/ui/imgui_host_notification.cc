/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cmath>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/imgui_host_notification.h"
#include "xenia/ui/imgui_notification.h"

namespace xe {
namespace ui {

ImGuiHostNotification::ImGuiHostNotification(ui::ImGuiDrawer* imgui_drawer,
                                             std::string& title,
                                             std::string& description,
                                             uint8_t user_index,
                                             uint8_t position_id)
    : ImGuiNotification(imgui_drawer, NotificationType::Host, title,
                        description, user_index, position_id) {
  SetCreationTime(Clock::QueryHostUptimeMillis() + notification_initial_delay);
  imgui_drawer->AddNotification(this);
}

ImGuiHostNotification::~ImGuiHostNotification() {}

const ImVec2 ImGuiHostNotification::CalculateNotificationSize(ImVec2 text_size,
                                                              float scale) {
  const ImVec2 result = ImVec2(text_size.x * scale, text_size.y * scale);

  return result;
}

void HostNotificationWindow::OnDraw(ImGuiIO& io) {
  if (IsNotificationClosingTime() || IsMarkedForDeletion()) {
    delete this;
    return;
  }

  // Adding 200ms of delay in case of notification spam.
  if (Clock::QueryHostUptimeMillis() < GetCreationTime()) {
    return;
  }

  const std::string longest_notification_text_line =
      GetTitle().size() > GetDescription().size() ? GetTitle().c_str()
                                                  : GetDescription().c_str();

  const ImVec2 screen_size = io.DisplaySize;
  const float window_scale =
      std::fminf(screen_size.x / default_drawing_resolution.x,
                 screen_size.y / default_drawing_resolution.y);
  const float font_size = io.Fonts->Fonts[0]->FontSize;
  const ImVec2 text_size = io.Fonts->Fonts[0]->CalcTextSizeA(
      font_size * window_scale, FLT_MAX, -1.0f,
      longest_notification_text_line.c_str());

  const ImVec2 notification_size =
      CalculateNotificationSize(text_size, window_scale);

  const ImVec2 notification_position = CalculateNotificationScreenPosition(
      screen_size, notification_size, GetPositionId());

  if (std::isnan(notification_position.x) ||
      std::isnan(notification_position.y)) {
    return;
  }

  ImGui::SetNextWindowPos(notification_position);

  ImGui::Begin("Notification Window", NULL, NOTIFY_TOAST_FLAGS);
  {
    ImGui::SetWindowFontScale(window_scale);

    ImGui::Text("%s", GetTitle().c_str());
    ImGui::Separator();
    ImGui::Text("%s", GetDescription().c_str());
  }
  ImGui::End();
}

}  // namespace ui
}  // namespace xe