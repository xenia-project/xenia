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
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_notification.h"

#if XE_PLATFORM_WIN32
#include <playsoundapi.h>
#endif

DEFINE_string(notification_sound_path, "",
              "Path (including filename) to selected notification sound. Sound "
              "MUST be in wav format!",
              "General");

namespace xe {
namespace ui {

ImGuiGuestNotification::ImGuiGuestNotification(ui::ImGuiDrawer* imgui_drawer,
                                               std::string& title,
                                               std::string& description,
                                               uint8_t user_index,
                                               uint8_t position_id)
    : ImGuiNotification(imgui_drawer, NotificationType::Guest, title,
                        description, user_index, position_id),
      current_stage_(NotificationStage::Awaiting),
      notification_draw_progress_(0.0f) {
  imgui_drawer->AddNotification(this);
}

ImGuiGuestNotification::~ImGuiGuestNotification() {}

void ImGuiGuestNotification::UpdateNotificationState() {
  switch (current_stage_) {
    case NotificationStage::Awaiting:
      // TODO(Gliniak): Implement delayed notifications.
      current_stage_ = NotificationStage::FazeIn;
      notification_draw_progress_ = 0.2f;
#if XE_PLATFORM_WIN32
      if (!cvars::notification_sound_path.empty()) {
        auto notification_sound_path = cvars::notification_sound_path;
        if (std::filesystem::exists(notification_sound_path)) {
          PlaySound(std::wstring(notification_sound_path.begin(),
                                 notification_sound_path.end())
                        .c_str(),
                    NULL,
                    SND_FILENAME | SND_NODEFAULT | SND_NOSTOP | SND_ASYNC);
        }
      }
#endif

      break;
    case NotificationStage::FazeIn: {
      SetCreationTime(Clock::QueryHostUptimeMillis());
      if (notification_draw_progress_ < 1.1f) {
        notification_draw_progress_ += 0.02f;
      }

      // Mimics a bit original console behaviour when it makes window a bit
      // longer for few frames then decreases size
      if (notification_draw_progress_ >= 1.1f) {
        current_stage_ = NotificationStage::Present;
        notification_draw_progress_ = 1.0f;
      }
      break;
    }
    case NotificationStage::Present:
      if (IsNotificationClosingTime()) {
        current_stage_ = NotificationStage::FazeOut;
      }
      break;
    case NotificationStage::FazeOut: {
      if (notification_draw_progress_ > 0.2f) {
        notification_draw_progress_ -= 0.02f;
      } else {
        current_stage_ = NotificationStage::Finished;
      }
      break;
    }
    default:
      break;
  }
}

const ImVec2 ImGuiGuestNotification::CalculateNotificationSize(ImVec2 text_size,
                                                               float scale) {
  const ImVec2 result = ImVec2(floorf((default_notification_icon_size.x +
                                       default_notification_margin_size.x) *
                                      scale) +
                                   text_size.x,
                               floorf((default_notification_icon_size.y +
                                       default_notification_margin_size.y) *
                                      scale));

  return result;
}

void AchievementNotificationWindow::OnDraw(ImGuiIO& io) {
  UpdateNotificationState();

  if (IsNotificationExpired()) {
    delete this;
    return;
  }

  const std::string longest_notification_text_line =
      GetTitle().size() > GetDescription().size() ? GetTitle().c_str()
                                                  : GetDescription().c_str();

  const ImVec2 screen_size = io.DisplaySize;
  const float window_scale =
      std::fminf(screen_size.x / default_drawing_resolution.x,
                 screen_size.y / default_drawing_resolution.y);
  const float font_scale = default_font_size / io.Fonts->Fonts[0]->FontSize;
  const ImVec2 text_size = io.Fonts->Fonts[0]->CalcTextSizeA(
      default_font_size * default_notification_text_scale * window_scale,
      FLT_MAX, -1.0f, longest_notification_text_line.c_str());

  const ImVec2 final_notification_size =
      CalculateNotificationSize(text_size, window_scale);

  const ImVec2 notification_position = CalculateNotificationScreenPosition(
      screen_size, final_notification_size, GetPositionId());

  if (std::isnan(notification_position.x) ||
      std::isnan(notification_position.y)) {
    return;
  }

  ImVec2 current_notification_size = final_notification_size;
  current_notification_size.x *= notification_draw_progress_;
  current_notification_size.x = floorf(current_notification_size.x);

  // Initialize position and window size
  ImGui::SetNextWindowSize(current_notification_size);
  ImGui::SetNextWindowPos(notification_position);

  // Set new window style before drawing window
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
                      default_notification_rounding * window_scale);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        default_notification_background_color);
  ImGui::PushStyleColor(ImGuiCol_Separator,
                        default_notification_background_color);
  ImGui::PushStyleColor(ImGuiCol_Border, default_notification_border_color);

  ImGui::Begin("Notification Window", NULL, NOTIFY_TOAST_FLAGS);
  {
    ImGui::SetWindowFontScale(default_notification_text_scale * font_scale *
                              window_scale);
    // Set offset to image to prevent it from being right on border.
    ImGui::SetCursorPos(ImVec2(final_notification_size.x * 0.005f,
                               final_notification_size.y * 0.05f));
    // Elements of window
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     GetDrawer()->GetNotificationIcon(GetUserIndex())),
                 ImVec2(default_notification_icon_size.x * window_scale,
                        default_notification_icon_size.y * window_scale));

    ImGui::SameLine();
    if (notification_draw_progress_ > 0.5f) {
      ImGui::TextColored(white_color, "%s", GetNotificationText().c_str());
    }
  }
  // Restore previous style
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  ImGui::End();
}

void XNotifyWindow::OnDraw(ImGuiIO& io) {
  UpdateNotificationState();

  if (IsNotificationExpired()) {
    delete this;
    return;
  }

  const std::string longest_notification_text_line =
      GetTitle().size() > GetDescription().size() ? GetTitle().c_str()
                                                  : GetDescription().c_str();

  const ImVec2 screen_size = io.DisplaySize;
  const float window_scale =
      std::fminf(screen_size.x / default_drawing_resolution.x,
                 screen_size.y / default_drawing_resolution.y);
  const float font_scale = default_font_size / io.Fonts->Fonts[0]->FontSize;
  const ImVec2 text_size = io.Fonts->Fonts[0]->CalcTextSizeA(
      default_font_size * default_notification_text_scale * window_scale,
      FLT_MAX, -1.0f, longest_notification_text_line.c_str());

  const ImVec2 final_notification_size =
      CalculateNotificationSize(text_size, window_scale);

  const ImVec2 notification_position = CalculateNotificationScreenPosition(
      screen_size, final_notification_size, GetPositionId());

  if (std::isnan(notification_position.x) ||
      std::isnan(notification_position.y)) {
    return;
  }

  ImVec2 current_notification_size = final_notification_size;
  current_notification_size.x *= notification_draw_progress_;
  current_notification_size.x = floorf(current_notification_size.x);

  // Initialize position and window size
  ImGui::SetNextWindowSize(current_notification_size);
  ImGui::SetNextWindowPos(notification_position);

  // Set new window style before drawing window
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
                      default_notification_rounding * window_scale);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        default_notification_background_color);
  ImGui::PushStyleColor(ImGuiCol_Separator,
                        default_notification_background_color);
  ImGui::PushStyleColor(ImGuiCol_Border, default_notification_border_color);

  ImGui::Begin("Notification Window", NULL, NOTIFY_TOAST_FLAGS);
  {
    ImGui::SetWindowFontScale(default_notification_text_scale * font_scale *
                              window_scale);
    // Set offset to image to prevent it from being right on border.
    ImGui::SetCursorPos(ImVec2(final_notification_size.x * 0.005f,
                               final_notification_size.y * 0.05f));
    // Elements of window
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     GetDrawer()->GetNotificationIcon(GetUserIndex())),
                 ImVec2(default_notification_icon_size.x * window_scale,
                        default_notification_icon_size.y * window_scale));

    // Set offset to image to prevent it from being right on border.
    // ImGui::SetCursorPos(ImVec2(final_notification_size.x * 0.1f,
    //                           final_notification_size.y * 0.2f));

    ImGui::SameLine();
    if (notification_draw_progress_ > 0.5f) {
      ImGui::TextColored(white_color, "%s", GetDescription().c_str());
    }
  }
  // Restore previous style
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  ImGui::End();
}

}  // namespace ui
}  // namespace xe