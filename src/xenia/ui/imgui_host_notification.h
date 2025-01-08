/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_HOST_NOTIFICATION_H_
#define XENIA_UI_IMGUI_HOST_NOTIFICATION_H_

#include "third_party/imgui/imgui.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_notification.h"

namespace xe {
namespace ui {
class ImGuiHostNotification : public ImGuiNotification {
 public:
  ImGuiHostNotification(ui::ImGuiDrawer* imgui_drawer, std::string& title,
                        std::string& description, uint8_t user_index,
                        uint8_t position_id = 10);

  ~ImGuiHostNotification();

 protected:
  const uint64_t notification_initial_delay =
      std::chrono::milliseconds(200).count();

  const ImVec2 CalculateNotificationSize(ImVec2 text_size,
                                         float scale) override;

  virtual void OnDraw(ImGuiIO& io) override {}
};

class HostNotificationWindow final : ImGuiHostNotification {
 public:
  HostNotificationWindow(ui::ImGuiDrawer* imgui_drawer, std::string title,
                         std::string description, uint8_t user_index,
                         uint8_t position_id = 10)
      : ImGuiHostNotification(imgui_drawer, title, description, user_index,
                              position_id) {};

  void OnDraw(ImGuiIO& io) override;
};

}  // namespace ui
}  // namespace xe

#endif