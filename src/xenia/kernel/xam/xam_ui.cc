/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "third_party/imgui/imgui.h"
#include "xenia/app/profile_dialogs.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/base/system.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

#include "third_party/fmt/include/fmt/chrono.h"

DEFINE_bool(storage_selection_dialog, false,
            "Show storage device selection dialog when the game requests it.",
            "UI");

DECLARE_int32(license_mask);

namespace xe {
namespace kernel {
namespace xam {
// TODO(gibbed): This is all one giant WIP that seems to work better than the
// previous immediate synchronous completion of dialogs.
//
// The deferred execution of dialog handling is done in such a way that there is
// a pre-, peri- (completion), and post- callback steps.
//
// pre();
// result = completion();
// CompleteOverlapped(result);
// post();
//
// There are games that are batshit insane enough to wait for the X_OVERLAPPED
// to be completed (ie not X_ERROR_PENDING) before creating a listener to
// receive a notification, which is why we have distinct pre- and post- steps.
//
// We deliberately delay the XN_SYS_UI = false notification to give games time
// to create a listener (if they're insane enough do this).

extern std::atomic<int> xam_dialogs_shown_;

constexpr ImVec2 default_image_icon_size = ImVec2(75.f, 75.f);

class XamDialog : public xe::ui::ImGuiDialog {
 public:
  void set_close_callback(std::function<void()> close_callback) {
    close_callback_ = close_callback;
  }

 protected:
  XamDialog(xe::ui::ImGuiDrawer* imgui_drawer)
      : xe::ui::ImGuiDialog(imgui_drawer) {}

  virtual ~XamDialog() {}
  void OnClose() override {
    if (close_callback_) {
      close_callback_();
    }
  }

 private:
  std::function<void()> close_callback_ = nullptr;
};

template <typename T>
X_RESULT xeXamDispatchDialog(T* dialog,
                             std::function<X_RESULT(T*)> close_callback,
                             uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback]() -> X_RESULT {
    X_RESULT result;
    dialog->set_close_callback([&dialog, &result, &close_callback]() {
      result = close_callback(dialog);
    });
    xe::threading::Fence fence;
    xe::ui::WindowedAppContext& app_context =
        kernel_state()->emulator()->display_window()->app_context();
    if (app_context.CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    auto result = run();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogEx(
    T* dialog, std::function<X_RESULT(T*, uint32_t&, uint32_t&)> close_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback](uint32_t& extended_error,
                                      uint32_t& length) -> X_RESULT {
    auto display_window = kernel_state()->emulator()->display_window();
    X_RESULT result;
    dialog->set_close_callback(
        [&dialog, &result, &extended_error, &length, &close_callback]() {
          result = close_callback(dialog, extended_error, length);
        });
    xe::threading::Fence fence;
    if (display_window->app_context().CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadless(std::function<X_RESULT()> run_callback,
                               uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    auto result = run_callback();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run_callback, overlapped, pre,
                                               post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadlessEx(
    std::function<X_RESULT(uint32_t&, uint32_t&)> run_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run_callback(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run_callback, overlapped, pre,
                                                 post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogAsync(T* dialog,
                                  std::function<void(T*)> close_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  ++xam_dialogs_shown_;

  // Important to pass captured vars by value here since we return from this
  // without waiting for the dialog to close so the original local vars will be
  // destroyed.
  dialog->set_close_callback([dialog, close_callback]() {
    close_callback(dialog);

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

X_RESULT xeXamDispatchHeadlessAsync(std::function<void()> run_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  ++xam_dialogs_shown_;

  auto display_window = kernel_state()->emulator()->display_window();
  display_window->app_context().CallInUIThread([run_callback]() {
    run_callback();

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

dword_result_t XamIsUIActive_entry() { return xeXamIsUIActive(); }
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

class MessageBoxDialog : public XamDialog {
 public:
  MessageBoxDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string& title,
                   std::string& description, std::vector<std::string> buttons,
                   uint32_t default_button)
      : XamDialog(imgui_drawer),
        title_(title),
        description_(description),
        buttons_(std::move(buttons)),
        default_button_(default_button),
        chosen_button_(default_button) {
    if (!title_.size()) {
      title_ = "Message Box";
    }
  }

  uint32_t chosen_button() const { return chosen_button_; }

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (description_.size()) {
        ImGui::Text("%s", description_.c_str());
      }
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      for (size_t i = 0; i < buttons_.size(); ++i) {
        if (ImGui::Button(buttons_[i].c_str())) {
          chosen_button_ = static_cast<uint32_t>(i);
          ImGui::CloseCurrentPopup();
          Close();
        }
        ImGui::SameLine();
      }
      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::EndPopup();
    } else {
      Close();
    }
  }
  virtual ~MessageBoxDialog() {}

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::vector<std::string> buttons_;
  uint32_t default_button_ = 0;
  uint32_t chosen_button_ = 0;
};

class ProfilePasscodeDialog : public XamDialog {
 public:
  const char* labelled_keys_[11] = {"None", "X",  "Y",    "RB",   "LB",   "LT",
                                    "RT",   "Up", "Down", "Left", "Right"};

  const std::map<std::string, uint16_t> keys_map_ = {
      {"None", 0},
      {"X", X_BUTTON_PASSCODE},
      {"Y", Y_BUTTON_PASSCODE},
      {"RB", RIGHT_BUMPER_PASSCODE},
      {"LB", LEFT_BUMPER_PASSCODE},
      {"LT", LEFT_TRIGGER_PASSCODE},
      {"RT", RIGHT_TRIGGER_PASSCODE},
      {"Up", DPAD_UP_PASSCODE},
      {"Down", DPAD_DOWN_PASSCODE},
      {"Left", DPAD_LEFT_PASSCODE},
      {"Right", DPAD_RIGHT_PASSCODE}};

  ProfilePasscodeDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string& title,
                        std::string& description, MESSAGEBOX_RESULT* result_ptr)
      : XamDialog(imgui_drawer),
        title_(title),
        description_(description),
        result_ptr_(result_ptr) {
    std::memset(result_ptr, 0, sizeof(MESSAGEBOX_RESULT));

    if (title_.empty()) {
      title_ = "Enter Pass Code";
    }

    if (description_.empty()) {
      description_ = "Enter your Xbox LIVE pass code.";
    }
  }

  void DrawPasscodeField(uint8_t key_id) {
    const std::string label = fmt::format("##Key {}", key_id);

    if (ImGui::BeginCombo(label.c_str(),
                          labelled_keys_[key_indexes_[key_id]])) {
      for (uint8_t key_index = 0; key_index < keys_map_.size(); key_index++) {
        bool is_selected = key_id == key_index;

        if (ImGui::Selectable(labelled_keys_[key_index], is_selected)) {
          key_indexes_[key_id] = key_index;
        }

        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }

      ImGui::EndCombo();
    }
  }

  void OnDraw(ImGuiIO& io) override {
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
    }

    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (description_.size()) {
        ImGui::Text("%s", description_.c_str());
      }

      for (uint8_t i = 0; i < passcode_length; i++) {
        DrawPasscodeField(i);
        // result_ptr_->Passcode[i] =
        // keys_map_.at(labelled_keys_[key_indexes_[i]]);
      }

      ImGui::NewLine();

      // We write each key on close to prevent simultaneous dialogs.
      if (ImGui::Button("Sign In")) {
        for (uint8_t i = 0; i < passcode_length; i++) {
          result_ptr_->Passcode[i] =
              keys_map_.at(labelled_keys_[key_indexes_[i]]);
        }

        selected_signed_in_ = true;

        Close();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel")) {
        Close();
      }
    }

    ImGui::EndPopup();
  }

  virtual ~ProfilePasscodeDialog() {}

  bool SelectedSignedIn() const { return selected_signed_in_; }

 private:
  bool has_opened_ = false;
  bool selected_signed_in_ = false;
  std::string title_;
  std::string description_;

  static const uint8_t passcode_length = sizeof(X_XAMACCOUNTINFO::passcode);
  int key_indexes_[passcode_length] = {0, 0, 0, 0};
  MESSAGEBOX_RESULT* result_ptr_;
};

class GamertagModifyDialog final : public ui::ImGuiDialog {
 public:
  GamertagModifyDialog(ui::ImGuiDrawer* imgui_drawer,
                       ProfileManager* profile_manager, uint64_t xuid)
      : ui::ImGuiDialog(imgui_drawer),
        profile_manager_(profile_manager),
        xuid_(xuid) {
    memset(gamertag_, 0, sizeof(gamertag_));
  }

 private:
  void OnDraw(ImGuiIO& io) override {
    if (!has_opened_) {
      ImGui::OpenPopup("Modify Gamertag");
      has_opened_ = true;
    }

    bool dialog_open = true;
    if (!ImGui::BeginPopupModal("Modify Gamertag", &dialog_open,
                                ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_HorizontalScrollbar)) {
      Close();
      return;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) {
      ImGui::SetKeyboardFocusHere(0);
    }

    ImGui::TextUnformatted("New gamertag:");
    ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

    const std::string gamertag_string = std::string(gamertag_);
    bool valid = profile_manager_->IsGamertagValid(gamertag_string);

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Update")) {
      profile_manager_->ModifyGamertag(xuid_, gamertag_string);
      std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
      dialog_open = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
      std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
      dialog_open = false;
    }

    if (!dialog_open) {
      ImGui::CloseCurrentPopup();
      Close();
      ImGui::EndPopup();
      return;
    }
    ImGui::EndPopup();
  };

  bool has_opened_ = false;
  char gamertag_[16] = "";
  const uint64_t xuid_;
  ProfileManager* profile_manager_;
};

struct AchievementInfo {
  uint32_t id;
  std::u16string name;
  std::u16string desc;
  std::u16string unachieved;
  uint32_t gamerscore;
  uint32_t image_id;
  uint32_t flags;
  std::chrono::local_time<std::chrono::system_clock::duration> unlock_time;

  bool IsUnlocked() const {
    return (flags & static_cast<uint32_t>(AchievementFlags::kAchieved)) ||
           flags & static_cast<uint32_t>(AchievementFlags::kAchievedOnline);
  }

  // Unlocked online means that unlock time is confirmed and valid!
  bool IsUnlockedOnline() const {
    return (flags & static_cast<uint32_t>(AchievementFlags::kAchievedOnline));
  }
};

struct TitleInfo {
  std::string title_name;
  uint32_t id;
  uint32_t unlocked_achievements_count;
  uint32_t achievements_count;
  uint32_t title_earned_gamerscore;
  uint64_t last_played;  // Convert from guest to some tm?
};

class GameAchievementsDialog final : public XamDialog {
 public:
  GameAchievementsDialog(ui::ImGuiDrawer* imgui_drawer,
                         const ImVec2 drawing_position,
                         const TitleInfo* title_info,
                         const UserProfile* profile)
      : XamDialog(imgui_drawer),
        drawing_position_(drawing_position),
        title_info_(*title_info),
        profile_(profile) {
    LoadAchievementsData();
  }

 private:
  bool LoadAchievementsData() {
    xe::ui::IconsData data;

    const auto title_achievements =
        kernel_state()
            ->xam_state()
            ->achievement_manager()
            ->GetTitleAchievements(profile_->xuid(), title_info_.id);

    const auto title_gpd = kernel_state()->title_xdbf();

    if (!title_achievements) {
      return false;
    }

    for (const auto& entry : *title_achievements) {
      AchievementInfo info;
      info.id = entry.achievement_id;
      info.name =
          xe::load_and_swap<std::u16string>(entry.achievement_name.c_str());
      info.desc =
          xe::load_and_swap<std::u16string>(entry.unlocked_description.c_str());
      info.unachieved =
          xe::load_and_swap<std::u16string>(entry.locked_description.c_str());

      info.flags = entry.flags;
      info.gamerscore = entry.gamerscore;
      info.image_id = entry.image_id;
      info.unlock_time = {};

      if (entry.IsUnlocked()) {
        info.unlock_time =
            chrono::WinSystemClock::to_local(entry.unlock_time.to_time_point());
      }

      achievements_info_.insert({info.id, info});

      const auto& icon_entry =
          title_gpd.GetEntry(util::XdbfSection::kImage, info.image_id);

      data.insert({info.image_id,
                   std::make_pair(icon_entry.buffer,
                                  static_cast<uint32_t>(icon_entry.size))});
    }

    achievements_icons_ = imgui_drawer()->LoadIcons(data);
    return true;
  }

  std::string GetAchievementTitle(const AchievementInfo& achievement_entry) {
    std::string title = "Secret trophy";

    if (achievement_entry.IsUnlocked() || show_locked_info_ ||
        achievement_entry.flags &
            static_cast<uint32_t>(AchievementFlags::kShowUnachieved)) {
      title = xe::to_utf8(achievement_entry.name);
    }

    return title;
  }

  std::string GetAchievementDescription(
      const AchievementInfo& achievement_entry) {
    std::string description = "Hidden description";

    if (achievement_entry.flags &
        static_cast<uint32_t>(AchievementFlags::kShowUnachieved)) {
      description = xe::to_utf8(achievement_entry.unachieved);
    }

    if (achievement_entry.IsUnlocked() || show_locked_info_) {
      description = xe::to_utf8(achievement_entry.desc);
    }

    return description;
  }

  void DrawTitleAchievementInfo(ImGuiIO& io,
                                const AchievementInfo& achievement_entry) {
    const auto start_drawing_pos = ImGui::GetCursorPos();

    ImGui::TableSetColumnIndex(0);
    if (achievement_entry.IsUnlocked() || show_locked_info_) {
      if (achievements_icons_.count(achievement_entry.image_id)) {
        ImGui::Image(achievements_icons_.at(achievement_entry.image_id).get(),
                     default_image_icon_size);
      } else {
        // Case when for whatever reason there is no icon available.
        ImGui::Image(0, default_image_icon_size);
      }
    } else {
      ImGui::Image(imgui_drawer()->GetLockedAchievementIcon(),
                   default_image_icon_size);
    }

    ImGui::TableNextColumn();

    ImGui::PushFont(imgui_drawer()->GetTitleFont());
    const auto primary_line_height = ImGui::GetTextLineHeight();
    ImGui::Text("%s", GetAchievementTitle(achievement_entry).c_str());
    ImGui::PopFont();

    ImGui::PushTextWrapPos(ImGui::GetMainViewport()->Size.x * 0.5f);
    ImGui::TextWrapped("%s",
                       GetAchievementDescription(achievement_entry).c_str());
    ImGui::PopTextWrapPos();

    ImGui::SetCursorPosY(start_drawing_pos.y + default_image_icon_size.x -
                         ImGui::GetTextLineHeight());

    if (achievement_entry.IsUnlocked()) {
      if (achievement_entry.IsUnlockedOnline()) {
        ImGui::TextUnformatted(fmt::format("Unlocked: {:%Y-%m-%d %H:%M}",
                                           achievement_entry.unlock_time)
                                   .c_str());
      } else {
        ImGui::TextUnformatted(fmt::format("Unlocked: Locally").c_str());
      }
    }

    ImGui::TableNextColumn();

    // TODO(Gliniak): There is no easy way to align text to middle, so I have to
    // do it manually.
    const float achievement_row_middle_alignment =
        ((default_image_icon_size.x / 2.f) - ImGui::GetTextLineHeight() / 2.f) *
        0.85f;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         achievement_row_middle_alignment);
    ImGui::PushFont(imgui_drawer()->GetTitleFont());
    ImGui::TextUnformatted(
        fmt::format("{} G", achievement_entry.gamerscore).c_str());
    ImGui::PopFont();
  }

  void OnDraw(ImGuiIO& io) override {
    ImGui::SetNextWindowPos(drawing_position_, ImGuiCond_FirstUseEver);

    const auto xenia_window_size = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(xenia_window_size.x * 0.2f, xenia_window_size.y * 0.3f),
        ImVec2(xenia_window_size.x * 0.6f, xenia_window_size.y * 0.8f));
    ImGui::SetNextWindowBgAlpha(0.8f);

    bool dialog_open = true;

    if (!ImGui::Begin(
            fmt::format("{} Achievements List", title_info_.title_name).c_str(),
            &dialog_open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_HorizontalScrollbar)) {
      Close();
      ImGui::End();
      return;
    }

    ImGui::Checkbox("Show locked achievements information", &show_locked_info_);
    ImGui::Separator();

    if (achievements_info_.empty()) {
      ImGui::TextUnformatted(fmt::format("No achievements data!").c_str());
    } else {
      if (ImGui::BeginTable("", 3,
                            ImGuiTableFlags_::ImGuiTableFlags_BordersInnerH)) {
        for (const auto& [_, entry] : achievements_info_) {
          ImGui::TableNextRow(0, default_image_icon_size.y);
          DrawTitleAchievementInfo(io, entry);
        }

        ImGui::EndTable();
      }
    }

    if (!dialog_open) {
      Close();
      ImGui::End();
      return;
    }

    ImGui::End();
  };

  bool show_locked_info_ = false;

  const ImVec2 drawing_position_ = {};

  const TitleInfo title_info_;
  const UserProfile* profile_;

  std::map<uint32_t, AchievementInfo> achievements_info_;
  std::map<uint32_t, std::unique_ptr<ui::ImmediateTexture>> achievements_icons_;
};

class GamesInfoDialog final : public ui::ImGuiDialog {
 public:
  GamesInfoDialog(ui::ImGuiDrawer* imgui_drawer, const ImVec2 drawing_position,
                  const UserProfile* profile)
      : ui::ImGuiDialog(imgui_drawer),
        drawing_position_(drawing_position),
        profile_(profile),
        dialog_name_(fmt::format("{}'s Games List", profile->name())) {
    LoadProfileGameInfo(imgui_drawer, profile);
  }

 private:
  void LoadProfileGameInfo(ui::ImGuiDrawer* imgui_drawer,
                           const UserProfile* profile) {
    info_.clear();

    // TODO(Gliniak): This code should be adjusted for GPD support. Instead of
    // using whole profile it should only take vector of gpd entries. Ideally
    // remapped to another struct.
    if (kernel_state()->emulator()->is_title_open()) {
      const auto xdbf = kernel_state()->title_xdbf();

      if (!xdbf.is_valid()) {
        return;
      }

      const auto title_summary_info =
          kernel_state()->achievement_manager()->GetTitleAchievementsInfo(
              profile->xuid(), kernel_state()->title_id());

      if (!title_summary_info) {
        return;
      }

      TitleInfo game;
      game.id = kernel_state()->title_id();
      game.title_name = xdbf.title();
      game.title_earned_gamerscore = title_summary_info->gamerscore;
      game.unlocked_achievements_count =
          title_summary_info->unlocked_achievements_count;
      game.achievements_count = title_summary_info->achievements_count;
      game.last_played = 0;

      xe::ui::IconsData data;
      const auto& image_data = xdbf.icon();
      data[game.id] = {image_data.buffer, (uint32_t)image_data.size};

      title_icon = imgui_drawer->LoadIcons(data);
      info_.insert({game.id, game});
    }
  }

  void DrawTitleEntry(ImGuiIO& io, const TitleInfo& entry) {
    const auto start_position = ImGui::GetCursorPos();
    const ImVec2 next_window_position =
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
               ImGui::GetWindowPos().y);

    // First Column
    ImGui::TableSetColumnIndex(0);
    ImGui::Image(title_icon.count(entry.id) ? title_icon.at(entry.id).get() : 0,
                 default_image_icon_size);

    // Second Column
    ImGui::TableNextColumn();
    ImGui::PushFont(imgui_drawer()->GetTitleFont());
    ImGui::TextUnformatted(entry.title_name.c_str());
    ImGui::PopFont();

    ImGui::TextUnformatted(
        fmt::format("{}/{} Achievements unlocked ({} Gamerscore)",
                    entry.unlocked_achievements_count, entry.achievements_count,
                    entry.title_earned_gamerscore)
            .c_str());

    ImGui::SetCursorPosY(start_position.y + default_image_icon_size.y -
                         ImGui::GetTextLineHeight());

    // TODO(Gliniak): For now I left hardcoded now, but in the future it must be
    // changed to include last time of boot.
    ImGui::TextUnformatted(fmt::format("Last played: {}", "Now").c_str());

    ImGui::SetCursorPos(start_position);

    if (ImGui::Selectable("##Selectable", false,
                          ImGuiSelectableFlags_SpanAllColumns,
                          ImGui::GetContentRegionAvail())) {
      new GameAchievementsDialog(imgui_drawer(), next_window_position, &entry,
                                 profile_);
    }
  }

  void OnDraw(ImGuiIO& io) override {
    ImGui::SetNextWindowPos(drawing_position_, ImGuiCond_FirstUseEver);
    const auto xenia_window_size = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(xenia_window_size.x * 0.05f, xenia_window_size.y * 0.05f),
        ImVec2(xenia_window_size.x * 0.4f, xenia_window_size.y * 0.5f));
    ImGui::SetNextWindowBgAlpha(0.8f);

    bool dialog_open = true;
    if (!ImGui::Begin(dialog_name_.c_str(), &dialog_open,
                      ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_HorizontalScrollbar)) {
      ImGui::End();
      return;
    }

    if (!info_.empty()) {
      if (ImGui::BeginTable("", 2,
                            ImGuiTableFlags_::ImGuiTableFlags_BordersInnerH)) {
        for (const auto& [_, entry] : info_) {
          ImGui::TableNextRow(0, default_image_icon_size.y);
          DrawTitleEntry(io, entry);
        }

        ImGui::EndTable();
      }
    } else {
      // Align text to the center
      std::string no_entries_message = "There are no titles, so far.";

      ImGui::PushFont(imgui_drawer()->GetTitleFont());
      float windowWidth = ImGui::GetContentRegionAvail().x;
      ImVec2 textSize = ImGui::CalcTextSize(no_entries_message.c_str());
      float textOffsetX = (windowWidth - textSize.x) * 0.5f;
      if (textOffsetX > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
      }

      ImGui::Text("%s", no_entries_message.c_str());
      ImGui::PopFont();
    }

    ImGui::End();

    if (!dialog_open) {
      delete this;
      return;
    }
  }

  std::string dialog_name_ = "";
  const ImVec2 drawing_position_ = {};

  const UserProfile* profile_;

  std::map<uint32_t, std::unique_ptr<ui::ImmediateTexture>> title_icon;
  std::map<uint32_t, TitleInfo> info_;
};

static dword_result_t XamShowMessageBoxUi(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  std::string title = title_ptr ? xe::to_utf8(title_ptr.value()) : "";
  std::string text = text_ptr ? xe::to_utf8(text_ptr.value()) : "";

  std::vector<std::string> buttons;
  for (uint32_t i = 0; i < button_count; ++i) {
    uint32_t button_ptr = button_ptrs[i];
    auto button = xe::load_and_swap<std::u16string>(
        kernel_state()->memory()->TranslateVirtual(button_ptr));
    buttons.push_back(xe::to_utf8(button));
  }

  X_RESULT result;
  if (cvars::headless) {
    // Auto-pick the focused button.
    auto run = [result_ptr, active_button]() -> X_RESULT {
      result_ptr->ButtonPressed = static_cast<uint32_t>(active_button);
      return X_ERROR_SUCCESS;
    };

    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    switch (flags & 0xF) {
      case XMBox_NOICON: {
      } break;
      case XMBox_ERRORICON: {
      } break;
      case XMBox_WARNINGICON: {
      } break;
      case XMBox_ALERTICON: {
      } break;
    }

    const Emulator* emulator = kernel_state()->emulator();
    ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    if (flags & XMBox_PASSCODEMODE || flags & XMBox_VERIFYPASSCODEMODE) {
      auto close = [result_ptr,
                    active_button](ProfilePasscodeDialog* dialog) -> X_RESULT {
        if (dialog->SelectedSignedIn()) {
          // Logged in
          return X_ERROR_SUCCESS;
        } else {
          return X_ERROR_FUNCTION_FAILED;
        }
      };

      result = xeXamDispatchDialog<ProfilePasscodeDialog>(
          new ProfilePasscodeDialog(imgui_drawer, title, text, result_ptr),
          close, overlapped);
    } else {
      auto close = [result_ptr](MessageBoxDialog* dialog) -> X_RESULT {
        result_ptr->ButtonPressed = dialog->chosen_button();
        return X_ERROR_SUCCESS;
      };

      result = xeXamDispatchDialog<MessageBoxDialog>(
          new MessageBoxDialog(imgui_drawer, title, text, buttons,
                               static_cast<uint32_t>(active_button)),
          close, overlapped);
    }
  }

  return result;
}

// https://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/
dword_result_t XamShowMessageBoxUI_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUI, kUI, kImplemented);

dword_result_t XamShowMessageBoxUIEx_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, dword_t unknown_unused,
    pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUIEx, kUI, kImplemented);

dword_result_t XNotifyQueueUI_entry(dword_t exnq, dword_t dwUserIndex,
                                    qword_t qwAreas,
                                    lpu16string_t displayText_ptr,
                                    lpvoid_t contextData) {
  std::string displayText = "";
  const uint8_t position_id = static_cast<uint8_t>(qwAreas);

  if (displayText_ptr) {
    displayText = xe::to_utf8(displayText_ptr.value());
  }

  XELOGI("XNotifyQueueUI: {}", displayText);

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  new xe::ui::XNotifyWindow(imgui_drawer, "", displayText, dwUserIndex,
                            position_id);

  // XNotifyQueueUI -> XNotifyQueueUIEx -> XMsgProcessRequest ->
  // XMsgStartIORequestEx & XMsgInProcessCall
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNotifyQueueUI, kUI, kSketchy);

class KeyboardInputDialog : public XamDialog {
 public:
  KeyboardInputDialog(xe::ui::ImGuiDrawer* imgui_drawer, std::string& title,
                      std::string& description, std::string& default_text,
                      size_t max_length)
      : XamDialog(imgui_drawer),
        title_(title),
        description_(description),
        default_text_(default_text),
        max_length_(max_length),
        text_buffer_() {
    if (!title_.size()) {
      if (!description_.size()) {
        title_ = "Keyboard Input";
      } else {
        title_ = description_;
        description_ = "";
      }
    }
    text_ = default_text;
    text_buffer_.resize(max_length);
    xe::string_util::copy_truncating(text_buffer_.data(), default_text_,
                                     text_buffer_.size());
  }
  virtual ~KeyboardInputDialog() {}

  const std::string& text() const { return text_; }
  bool cancelled() const { return cancelled_; }

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (description_.size()) {
        ImGui::TextWrapped("%s", description_.c_str());
      }
      if (first_draw) {
        ImGui::SetKeyboardFocusHere();
      }
      if (ImGui::InputText("##body", text_buffer_.data(), text_buffer_.size(),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        text_ = std::string(text_buffer_.data(), text_buffer_.size());
        cancelled_ = false;
        ImGui::CloseCurrentPopup();
        Close();
      }
      if (ImGui::Button("OK")) {
        text_ = std::string(text_buffer_.data(), text_buffer_.size());
        cancelled_ = false;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        text_ = "";
        cancelled_ = true;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::Spacing();
      ImGui::EndPopup();
    } else {
      Close();
    }
  }

 private:
  bool has_opened_ = false;
  std::string title_;
  std::string description_;
  std::string default_text_;
  size_t max_length_ = 0;
  std::vector<char> text_buffer_;
  std::string text_ = "";
  bool cancelled_ = true;
};

// https://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI_entry(
    dword_t user_index, dword_t flags, lpu16string_t default_text,
    lpu16string_t title, lpu16string_t description, lpu16string_t buffer,
    dword_t buffer_length, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  assert_not_null(overlapped);

  auto buffer_size = static_cast<size_t>(buffer_length) * 2;

  X_RESULT result;
  if (cvars::headless) {
    auto run = [default_text, buffer, buffer_length,
                buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {
        string_util::copy_and_swap_truncating(buffer, default_text.value(),
                                              buffer_length);
      }
      return X_ERROR_SUCCESS;
    };
    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    auto close = [buffer, buffer_length](KeyboardInputDialog* dialog,
                                         uint32_t& extended_error,
                                         uint32_t& length) -> X_RESULT {
      if (dialog->cancelled()) {
        extended_error = X_ERROR_CANCELLED;
        length = 0;
        return X_ERROR_SUCCESS;
      } else {
        // Zero the output buffer.
        auto text = xe::to_utf16(dialog->text());
        string_util::copy_and_swap_truncating(buffer, text, buffer_length);
        extended_error = X_ERROR_SUCCESS;
        length = 0;
        return X_ERROR_SUCCESS;
      }
    };
    const Emulator* emulator = kernel_state()->emulator();
    ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    std::string title_str = title ? xe::to_utf8(title.value()) : "";
    std::string desc_str = description ? xe::to_utf8(description.value()) : "";
    std::string def_text_str =
        default_text ? xe::to_utf8(default_text.value()) : "";

    result = xeXamDispatchDialogEx<KeyboardInputDialog>(
        new KeyboardInputDialog(imgui_drawer, title_str, desc_str, def_text_str,
                                buffer_length),
        close, overlapped);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI_entry(
    dword_t user_index, dword_t content_type, dword_t content_flags,
    qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!overlapped) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if ((user_index >= XUserMaxUserCount && user_index != XUserIndexAny) ||
      (content_flags & 0x83F00008) != 0 || !device_id_ptr) {
    XOverlappedSetExtendedError(overlapped, X_ERROR_INVALID_PARAMETER);
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny &&
      !kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                X_ERROR_NO_SUCH_USER);
    return X_ERROR_IO_PENDING;
  }

  std::vector<const DummyDeviceInfo*> devices = ListStorageDevices();

  if (cvars::headless || !cvars::storage_selection_dialog) {
    // Default to the first storage device (HDD) if headless.
    return xeXamDispatchHeadless(
        [device_id_ptr, devices]() -> X_RESULT {
          if (devices.empty()) return X_ERROR_CANCELLED;

          const DummyDeviceInfo* device_info = devices.front();
          *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [device_id_ptr, devices](MessageBoxDialog* dialog) -> X_RESULT {
    uint32_t button = dialog->chosen_button();
    if (button >= devices.size()) return X_ERROR_CANCELLED;

    const DummyDeviceInfo* device_info = devices.at(button);
    *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
    return X_ERROR_SUCCESS;
  };

  std::string title = "Select storage device";
  std::string desc = "";

  cxxopts::OptionNames buttons;
  for (auto& device_info : devices) {
    buttons.push_back(to_utf8(device_info->name));
  }
  buttons.push_back("Cancel");

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

void XamShowDirtyDiscErrorUI_entry(dword_t user_index) {
  if (cvars::headless) {
    assert_always();
    exit(1);
    return;
  }

  std::string title = "Disc Read Error";
  std::string desc =
      "There's been an issue reading content from the game disc.\nThis is "
      "likely caused by bad or unimplemented file IO calls.";

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, {"OK"}, 0),
      [](MessageBoxDialog*) -> X_RESULT { return X_ERROR_SUCCESS; }, 0);
  // This is death, and should never return.
  // TODO(benvanik): cleaner exit.
  exit(1);
}
DECLARE_XAM_EXPORT1(XamShowDirtyDiscErrorUI, kUI, kImplemented);

dword_result_t XamShowPartyUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowPartyUI, kNone, kStub);

dword_result_t XamShowCommunitySessionsUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowCommunitySessionsUI, kNone, kStub);

// this is supposed to do a lot more, calls another function that triggers some
// cbs
dword_result_t XamSetDashContext_entry(dword_t value,
                                       const ppc_context_t& ctx) {
  ctx->kernel_state->dash_context_ = value;
  kernel_state()->BroadcastNotification(
      kXNotificationDvdDriveUnknownDashContext, 0);
  return 0;
}

DECLARE_XAM_EXPORT1(XamSetDashContext, kNone, kImplemented);

dword_result_t XamGetDashContext_entry(const ppc_context_t& ctx) {
  return ctx->kernel_state->dash_context_;
}

DECLARE_XAM_EXPORT1(XamGetDashContext, kNone, kImplemented);

// https://gitlab.com/GlitchyScripts/xlivelessness/-/blob/master/xlivelessness/xlive/xdefs.hpp?ref_type=heads#L1235
X_HRESULT xeXShowMarketplaceUIEx(dword_t user_index, dword_t ui_type,
                                 qword_t offer_id, dword_t content_types,
                                 unknown_t unk5, unknown_t unk6, unknown_t unk7,
                                 unknown_t unk8) {
  // ui_type:
  // 0 - view all content for the current title
  // 1 - view content specified by offer id
  // content_types:
  // game specific, usually just -1
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([]() {});
  }

  auto close = [ui_type](MessageBoxDialog* dialog) -> void {
    if (ui_type == 1) {
      uint32_t button = dialog->chosen_button();
      if (button == 0) {
        cvars::license_mask = 1;

        kernel_state()->BroadcastNotification(
            kXNotificationLiveContentInstalled, 0);
      }
    }
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons;

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
      desc =
          "Game requested to open marketplace page with all content for the "
          "current title ID.";
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      desc = fmt::format(
          "Game requested to open marketplace page for offer ID 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipList:
      desc = fmt::format(
          "Game requested to open marketplace page with all xbox live "
          "memberships 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipItem:
      desc = fmt::format(
          "Game requested to open marketplace page for an xbox live "
          "memberships 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentList_Background:
      // Used when accessing microsoft points
      desc = fmt::format(
          "Xbox Marketplace requested access to Microsoft Points offer page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem_Background:
      // Used when accessing credit card information and calls
      // XamShowCreditCardUI
      desc = fmt::format(
          "Xbox Marketplace requested access to credit card information page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV1:
      // Used by XamShowForcedNameChangeUI v1888
      desc = fmt::format("Changing gamertag currently not implemented");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV2:
      // Used by XamShowForcedNameChangeUI NXE and up
      desc = fmt::format("Changing gamertag currently not implemented");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ProfileNameChange:
      // Used by dashboard when selecting change gamertag in profile menu
      desc = fmt::format("Changing gamertag currently not implemented");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ActiveDownloads:
      // Used in profile tabs when clicking active downloads
      desc = fmt::format(
          "There are no current plans to download files from xbox servers");
      break;
    default:
      desc = fmt::format("Unknown marketplace op {:d}",
                         static_cast<uint32_t>(ui_type));
      break;
  }

  desc +=
      "\nNote that since Xenia cannot access Xbox Marketplace, any DLC must be "
      "installed manually using File -> Install Content.";

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
    default:
      buttons.push_back("OK");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      desc +=
          "\n\nTo start trial games in full mode, set license_mask to 1 in "
          "Xenia config file.\n\nDo you wish to change license_mask to 1 for "
          "*this session*?";
      buttons.push_back("Yes");
      buttons.push_back("No");
      break;
  }

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close);
}

dword_result_t XamShowMarketplaceUI_entry(dword_t user_index, dword_t ui_type,
                                          qword_t offer_id,
                                          dword_t content_types, unknown_t unk5,
                                          unknown_t unk6) {
  return xeXShowMarketplaceUIEx(user_index, ui_type, offer_id, content_types,
                                unk5, 0, 0, unk6);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUI, kUI, kSketchy);

dword_result_t XamShowMarketplaceUIEx_entry(dword_t user_index, dword_t ui_type,
                                            qword_t offer_id,
                                            dword_t content_types,
                                            unknown_t unk5, unknown_t unk6,
                                            unknown_t unk7, unknown_t unk8) {
  return xeXShowMarketplaceUIEx(user_index, ui_type, offer_id, content_types,
                                unk5, unk6, unk7, unk8);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUIEx, kUI, kSketchy);

dword_result_t XamShowMarketplaceDownloadItemsUI_entry(
    dword_t user_index, dword_t ui_type, lpqword_t offers, dword_t num_offers,
    lpdword_t hresult_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  // ui_type:
  // 1000 - free
  // 1001 - paid
  if (user_index >= XUserMaxUserCount || !offers || num_offers > 6) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                  X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadless(
        [hresult_ptr]() -> X_RESULT {
          if (hresult_ptr) {
            *hresult_ptr = X_E_SUCCESS;
          }
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [hresult_ptr](MessageBoxDialog* dialog) -> X_RESULT {
    if (hresult_ptr) {
      // TODO
      *hresult_ptr = X_E_SUCCESS;
    }
    return X_ERROR_SUCCESS;
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons = {"OK"};

  switch (ui_type) {
    case 1000:
      desc =
          "Game requested to open download page for the following free offer "
          "IDs:";
      break;
    case 1001:
      desc =
          "Game requested to open download page for the following offer IDs:";
      break;
    default:
      return X_ERROR_INVALID_PARAMETER;
  }

  for (uint32_t i = 0; i < num_offers; i++) {
    desc += fmt::format("\n0x{:16X}", offers[i].get());
  }

  desc +=
      "\n\nNote that since Xenia cannot access Xbox Marketplace, any DLC "
      "must "
      "be installed manually using File -> Install Content.";

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceDownloadItemsUI, kUI, kSketchy);

dword_result_t XamShowForcedNameChangeUI_entry(dword_t user_index) {
  // Changes from 6 to 8 past NXE
  return xeXShowMarketplaceUIEx(user_index, 6, 0, 0xffffffff, 0, 0, 0, 0);
}
DECLARE_XAM_EXPORT1(XamShowForcedNameChangeUI, kUI, kImplemented);

bool xeDrawProfileContent(ui::ImGuiDrawer* imgui_drawer, const uint64_t xuid,
                          const uint8_t user_index,
                          const X_XAMACCOUNTINFO* account,
                          uint64_t* selected_xuid) {
  auto profile_manager = kernel_state()->xam_state()->profile_manager();

  const float default_image_size = 75.0f;
  const ImVec2 next_window_position =
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
             ImGui::GetWindowPos().y);
  const ImVec2 drawing_start_position = ImGui::GetCursorPos();
  ImVec2 current_drawing_position = ImGui::GetCursorPos();

  // In the future it can be replaced with profile icon.
  ImGui::Image(user_index < XUserMaxUserCount
                   ? imgui_drawer->GetNotificationIcon(user_index)
                   : nullptr,
               ImVec2(default_image_size, default_image_size));

  ImGui::SameLine();
  current_drawing_position = ImGui::GetCursorPos();
  ImGui::TextUnformatted(
      fmt::format("User: {}\n", account->GetGamertagString()).c_str());

  ImGui::SameLine();
  ImGui::SetCursorPos(current_drawing_position);
  ImGui::SetCursorPosY(current_drawing_position.y + ImGui::GetTextLineHeight());
  ImGui::TextUnformatted(fmt::format("XUID: {:016X}  \n", xuid).c_str());

  ImGui::SameLine();
  ImGui::SetCursorPos(current_drawing_position);
  ImGui::SetCursorPosY(current_drawing_position.y +
                       2 * ImGui::GetTextLineHeight());

  if (user_index != XUserIndexAny) {
    ImGui::TextUnformatted(
        fmt::format("Assigned to slot: {}\n", user_index + 1).c_str());
  } else {
    ImGui::TextUnformatted(fmt::format("Profile is not signed in").c_str());
  }

  const ImVec2 drawing_end_position = ImGui::GetCursorPos();

  if (xuid && selected_xuid) {
    ImGui::SetCursorPos(drawing_start_position);

    if (ImGui::Selectable(
            "##Selectable", *selected_xuid == xuid,
            ImGuiSelectableFlags_SpanAllColumns,
            ImVec2(drawing_end_position.x - drawing_start_position.x,
                   drawing_end_position.y - drawing_start_position.y))) {
      *selected_xuid = xuid;
    }

    if (ImGui::BeginPopupContextItem("Profile Menu")) {
      if (user_index == XUserIndexAny) {
        if (ImGui::MenuItem("Login")) {
          profile_manager->Login(xuid);
        }

        if (ImGui::BeginMenu("Login to slot:")) {
          for (uint8_t i = 1; i <= XUserMaxUserCount; i++) {
            if (ImGui::MenuItem(fmt::format("slot {}", i).c_str())) {
              profile_manager->Login(xuid, i - 1);
            }
          }
          ImGui::EndMenu();
        }
      } else {
        if (ImGui::MenuItem("Logout")) {
          profile_manager->Logout(user_index);
        }
      }

      ImGui::BeginDisabled(kernel_state()->emulator()->is_title_open());
      if (ImGui::BeginMenu("Modify")) {
        if (ImGui::MenuItem("Gamertag")) {
          new GamertagModifyDialog(imgui_drawer, profile_manager, xuid);
        }

        ImGui::MenuItem("Profile Icon (Unsupported)");
        ImGui::EndMenu();
      }
      ImGui::EndDisabled();

      const bool is_signedin = profile_manager->GetProfile(xuid) != nullptr;
      ImGui::BeginDisabled(!is_signedin);
      if (ImGui::MenuItem("Show Achievements")) {
        new GamesInfoDialog(imgui_drawer, next_window_position,
                            profile_manager->GetProfile(user_index));
      }
      ImGui::EndDisabled();

      if (ImGui::MenuItem("Show Content Directory")) {
        const auto path = profile_manager->GetProfileContentPath(
            xuid, kernel_state()->title_id());

        if (!std::filesystem::exists(path)) {
          std::filesystem::create_directories(path);
        }

        std::thread path_open(LaunchFileExplorer, path);
        path_open.detach();
      }

      if (!kernel_state()->emulator()->is_title_open()) {
        ImGui::Separator();
        if (ImGui::BeginMenu("Delete Profile")) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(
              fmt::format("You're about to delete profile: {} (XUID: {:016X}). "
                          "This will remove all data assigned to this profile "
                          "including savefiles. Are you sure?",
                          account->GetGamertagString(), xuid)
                  .c_str());
          ImGui::EndTooltip();

          if (ImGui::MenuItem("Yes, delete it!")) {
            profile_manager->DeleteProfile(xuid);
            ImGui::EndMenu();
            ImGui::EndPopup();
            return false;
          }

          ImGui::EndMenu();
        }
      }
      ImGui::EndPopup();
    }
  }

  return true;
}

class SigninDialog : public XamDialog {
 public:
  SigninDialog(xe::ui::ImGuiDrawer* imgui_drawer, uint32_t users_needed)
      : XamDialog(imgui_drawer),
        users_needed_(users_needed),
        title_("Sign In") {
    last_user_ = kernel_state()->emulator()->input_system()->GetLastUsedSlot();

    for (uint8_t slot = 0; slot < XUserMaxUserCount; slot++) {
      std::string name = fmt::format("Slot {:d}", slot + 1);
      slot_data_.push_back({slot, name});
    }
  }

  virtual ~SigninDialog() {}

  void OnDraw(ImGuiIO& io) override {
    bool first_draw = false;
    if (!has_opened_) {
      ImGui::OpenPopup(title_.c_str());
      has_opened_ = true;
      first_draw = true;
      ReloadProfiles(true);
    }
    if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      auto profile_manager = kernel_state()->xam_state()->profile_manager();

      for (uint32_t i = 0; i < users_needed_; i++) {
        ImGui::BeginGroup();

        std::vector<const char*> combo_items;
        int items_count = 0;
        int current_item = 0;

        // Fill slot list.
        std::vector<uint8_t> slots;
        slots.push_back(0xFF);
        combo_items.push_back("---");
        for (auto& elem : slot_data_) {
          // Select the slot or skip it if it's already used.
          bool already_taken = false;
          for (uint32_t j = 0; j < users_needed_; j++) {
            if (chosen_slots_[j] == elem.first) {
              if (i == j) {
                current_item = static_cast<int>(combo_items.size());
              } else {
                already_taken = true;
              }
              break;
            }
          }

          if (already_taken) {
            continue;
          }

          slots.push_back(elem.first);
          combo_items.push_back(elem.second.c_str());
        }
        items_count = static_cast<int>(combo_items.size());

        ImGui::BeginDisabled(users_needed_ == 1);
        ImGui::Combo(fmt::format("##Slot{:d}", i).c_str(), &current_item,
                     combo_items.data(), items_count);
        chosen_slots_[i] = slots[current_item];
        ImGui::EndDisabled();
        ImGui::Spacing();

        combo_items.clear();
        current_item = 0;

        // Fill profile list.
        std::vector<uint64_t> xuids;
        xuids.push_back(0);
        combo_items.push_back("---");
        if (chosen_slots_[i] != 0xFF) {
          for (auto& elem : profile_data_) {
            // Select the profile or skip it if it's already used.
            bool already_taken = false;
            for (uint32_t j = 0; j < users_needed_; j++) {
              if (chosen_xuids_[j] == elem.first) {
                if (i == j) {
                  current_item = static_cast<int>(combo_items.size());
                } else {
                  already_taken = true;
                }
                break;
              }
            }

            if (already_taken) {
              continue;
            }

            xuids.push_back(elem.first);
            combo_items.push_back(elem.second.c_str());
          }
        }
        items_count = static_cast<int>(combo_items.size());

        ImGui::BeginDisabled(chosen_slots_[i] == 0xFF);
        ImGui::Combo(fmt::format("##Profile{:d}", i).c_str(), &current_item,
                     combo_items.data(), items_count);
        chosen_xuids_[i] = xuids[current_item];
        ImGui::EndDisabled();
        ImGui::Spacing();

        // Draw profile badge.
        uint8_t slot = chosen_slots_[i];
        uint64_t xuid = chosen_xuids_[i];
        const auto account = profile_manager->GetAccount(xuid);

        if (slot == 0xFF || xuid == 0 || !account) {
          float ypos = ImGui::GetCursorPosY();
          ImGui::SetCursorPosY(ypos + ImGui::GetTextLineHeight() * 5);
        } else {
          xeDrawProfileContent(imgui_drawer(), xuid, slot, account, nullptr);
        }

        ImGui::EndGroup();
        if (i != (users_needed_ - 1) && (i == 0 || i == 2)) {
          ImGui::SameLine();
        }
      }

      ImGui::Spacing();

      if (ImGui::Button("Create Profile")) {
        creating_profile_ = true;
        ImGui::OpenPopup("Create Profile");
        first_draw = true;
      }
      ImGui::Spacing();

      if (creating_profile_) {
        if (ImGui::BeginPopupModal("Create Profile", nullptr,
                                   ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_HorizontalScrollbar)) {
          if (first_draw) {
            ImGui::SetKeyboardFocusHere();
          }

          ImGui::TextUnformatted("Gamertag:");
          ImGui::InputText("##Gamertag", gamertag_, sizeof(gamertag_));

          const std::string gamertag_string = gamertag_;
          bool valid = profile_manager->IsGamertagValid(gamertag_string);

          ImGui::BeginDisabled(!valid);
          if (ImGui::Button("Create")) {
            profile_manager->CreateProfile(gamertag_string, false);
            std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
            ImGui::CloseCurrentPopup();
            creating_profile_ = false;
            ReloadProfiles(false);
          }
          ImGui::EndDisabled();
          ImGui::SameLine();

          if (ImGui::Button("Cancel")) {
            std::fill(std::begin(gamertag_), std::end(gamertag_), '\0');
            ImGui::CloseCurrentPopup();
            creating_profile_ = false;
          }

          ImGui::EndPopup();
        } else {
          creating_profile_ = false;
        }
      }

      if (ImGui::Button("OK")) {
        std::map<uint8_t, uint64_t> profile_map;
        for (uint32_t i = 0; i < users_needed_; i++) {
          uint8_t slot = chosen_slots_[i];
          uint64_t xuid = chosen_xuids_[i];
          if (slot != 0xFF && xuid != 0) {
            profile_map[slot] = xuid;
          }
        }
        profile_manager->LoginMultiple(profile_map);

        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();

      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        Close();
      }

      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::EndPopup();
    } else {
      Close();
    }
  }

  void ReloadProfiles(bool first_draw) {
    auto profile_manager = kernel_state()->xam_state()->profile_manager();
    auto profiles = profile_manager->GetAccounts();

    profile_data_.clear();
    for (auto& [xuid, account] : *profiles) {
      profile_data_.push_back({xuid, account.GetGamertagString()});
    }

    if (first_draw) {
      // If only one user is requested, request last used controller to sign in.
      if (users_needed_ == 1) {
        chosen_slots_[0] = last_user_;
      } else {
        for (uint32_t i = 0; i < users_needed_; i++) {
          // TODO: Not sure about this, needs testing on real hardware.
          chosen_slots_[i] = i;
        }
      }

      // Default profile selection to profile that is already signed in.
      for (auto& elem : profile_data_) {
        uint64_t xuid = elem.first;
        uint8_t slot = profile_manager->GetUserIndexAssignedToProfile(xuid);
        for (uint32_t j = 0; j < users_needed_; j++) {
          if (chosen_slots_[j] != XUserIndexAny && slot == chosen_slots_[j]) {
            chosen_xuids_[j] = xuid;
          }
        }
      }
    }
  }

 private:
  bool has_opened_ = false;
  std::string title_;
  uint32_t users_needed_ = 1;
  uint32_t last_user_ = 0;

  std::vector<std::pair<uint8_t, std::string>> slot_data_;
  std::vector<std::pair<uint64_t, std::string>> profile_data_;
  uint8_t chosen_slots_[XUserMaxUserCount] = {};
  uint64_t chosen_xuids_[XUserMaxUserCount] = {};

  bool creating_profile_ = false;
  char gamertag_[16] = "";
};

X_RESULT xeXamShowSigninUI(uint32_t user_index, uint32_t users_needed,
                           uint32_t flags) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger sign in notification.
  if (users_needed != 1 && users_needed != 2 && users_needed != 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([users_needed]() {
      std::map<uint8_t, uint64_t> xuids;

      for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
        UserProfile* profile = kernel_state()->xam_state()->GetUserProfile(i);
        if (profile) {
          xuids[i] = profile->xuid();
          if (xuids.size() >= users_needed) break;
        }
      }

      kernel_state()->xam_state()->profile_manager()->LoginMultiple(xuids);
    });
  }

  auto close = [](SigninDialog* dialog) -> void {};

  const Emulator* emulator = kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<SigninDialog>(
      new SigninDialog(imgui_drawer, users_needed), close);
}

dword_result_t XamShowSigninUI_entry(dword_t users_needed, dword_t flags) {
  return xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kImplemented);

dword_result_t XamShowSigninUIp_entry(dword_t user_index, dword_t users_needed,
                                      dword_t flags) {
  return xeXamShowSigninUI(user_index, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUIp, kUserProfiles, kImplemented);

dword_result_t XamShowAchievementsUI_entry(dword_t user_index,
                                           dword_t unk_mask) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (!kernel_state()->title_xdbf().is_valid()) {
    return X_ERROR_FUNCTION_FAILED;
  }

  TitleInfo info = {};
  info.id = kernel_state()->title_id();
  info.title_name = kernel_state()->title_xdbf().title();

  ui::ImGuiDrawer* imgui_drawer = kernel_state()->emulator()->imgui_drawer();

  auto close = [](GameAchievementsDialog* dialog) -> void {};
  return xeXamDispatchDialogAsync<GameAchievementsDialog>(
      new GameAchievementsDialog(imgui_drawer, ImVec2(100.f, 100.f), &info,
                                 user),
      close);
}
DECLARE_XAM_EXPORT1(XamShowAchievementsUI, kUserProfiles, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);
