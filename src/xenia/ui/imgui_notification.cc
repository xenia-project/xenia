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
#include "xenia/ui/imgui_notification.h"

namespace xe {
namespace ui {

ImGuiNotification::ImGuiNotification(ui::ImGuiDrawer* imgui_drawer,
                                     NotificationType notification_type,
                                     std::string& title,
                                     std::string& description,
                                     uint8_t user_index, uint8_t position_id)
    : imgui_drawer_(imgui_drawer),
      notification_type_(notification_type),
      title_(title),
      description_(description),
      user_index_(user_index),
      position_(position_id),
      creation_time_(0) {}

ImGuiNotification::~ImGuiNotification() {
  imgui_drawer_->RemoveNotification(this);
}

void ImGuiNotification::Draw() { OnDraw(imgui_drawer_->GetIO()); }

const NotificationAlignment ImGuiNotification::GetNotificationAlignment(
    const uint8_t notification_position_id) const {
  NotificationAlignment alignment = NotificationAlignment::kAlignUnknown;

  if (notification_position_id >=
      notification_position_id_screen_offset.size()) {
    return alignment;
  }

  const ImVec2 screen_offset =
      notification_position_id_screen_offset.at(notification_position_id);

  if (screen_offset.x < 0.3f) {
    alignment = NotificationAlignment::kAlignLeft;
  } else if (screen_offset.x > 0.7f) {
    alignment = NotificationAlignment::kAlignRight;
  } else {
    alignment = NotificationAlignment::kAlignCenter;
  }

  return alignment;
}

const ImVec2 ImGuiNotification::CalculateNotificationScreenPosition(
    ImVec2 screen_size, ImVec2 window_size,
    uint8_t notification_position_id) const {
  ImVec2 result = {NAN, NAN};

  if (window_size.x >= screen_size.x || window_size.y >= screen_size.y) {
    return result;
  }

  const NotificationAlignment alignment =
      GetNotificationAlignment(notification_position_id);

  if (alignment == NotificationAlignment::kAlignUnknown) {
    return result;
  }

  const ImVec2 screen_offset =
      notification_position_id_screen_offset.at(notification_position_id);

  switch (alignment) {
    case NotificationAlignment::kAlignLeft:
      result.x = std::roundf(screen_size.x * screen_offset.x);
      break;

    case NotificationAlignment::kAlignRight:
      result.x = std::roundf((screen_size.x * screen_offset.x) - window_size.x);
      break;

    case NotificationAlignment::kAlignCenter:
      result.x = std::roundf((screen_size.x * 0.5f) - (window_size.x * 0.5f));
      break;

    default:
      break;
  }

  result.y = std::roundf(screen_size.y * screen_offset.y);
  return result;
}

}  // namespace ui
}  // namespace xe