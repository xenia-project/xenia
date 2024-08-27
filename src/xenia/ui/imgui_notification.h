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

enum class NotificationType : uint8_t { Guest = 0, Host = 1 };

namespace xe {
namespace ui {
class ImGuiNotification {
 public:
  ImGuiNotification(ui::ImGuiDrawer* imgui_drawer,
                    NotificationType notification_type, std::string& title,
                    std::string& description, uint8_t user_index,
                    uint8_t position_id = 0);

  ~ImGuiNotification();

  void Draw();

  const NotificationType GetNotificationType() const {
    return notification_type_;
  }

  void SetDeletionPending() { marked_for_deletion_ = true; }

 protected:
  ImGuiDrawer* GetDrawer() { return imgui_drawer_; }

  const bool IsNotificationClosingTime() const {
    const uint64_t current_time = Clock::QueryHostUptimeMillis();
    // We're before showing notification
    if (current_time < creation_time_) {
      return false;
    }
    return current_time - creation_time_ > time_to_close_;
  }

  const std::string GetNotificationText() const {
    std::string text = title_;

    if (!description_.empty()) {
      text.append("\n" + description_);
    }
    return text;
  }

  void SetCreationTime(uint64_t new_creation_time) {
    creation_time_ = new_creation_time;
  }

  const bool IsMarkedForDeletion() const { return marked_for_deletion_; }
  const std::string GetTitle() const { return title_; }
  const std::string GetDescription() const { return description_; }

  const uint8_t GetPositionId() const { return position_; }
  const uint8_t GetUserIndex() const { return user_index_; }
  const uint64_t GetCreationTime() const { return creation_time_; }

  const NotificationAlignment GetNotificationAlignment(
      const uint8_t notification_position_id) const;

  const ImVec2 CalculateNotificationScreenPosition(
      ImVec2 screen_size, ImVec2 window_size,
      uint8_t notification_position_id) const;

  virtual const ImVec2 CalculateNotificationSize(ImVec2 text_size,
                                                 float scale) = 0;

  virtual void OnDraw(ImGuiIO& io) {}

 private:
  NotificationType notification_type_;

  uint8_t position_;
  uint8_t user_index_;

  uint32_t delay_ = 0;
  uint32_t time_to_close_ = 4500;

  uint64_t creation_time_;

  std::string title_;
  std::string description_;

  bool marked_for_deletion_ = false;

  ImGuiDrawer* imgui_drawer_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif