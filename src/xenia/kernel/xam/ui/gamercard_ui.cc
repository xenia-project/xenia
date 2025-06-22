/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/base/png_utils.h"
#include "xenia/ui/file_picker.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

constexpr float leftSideTextObjectAlignment = 100.f;
constexpr float rightSideTextObjectAlignment = 140.f;
// Because all these ENUM->NAME conversions will be used only here there is no
// reason to store them somewhere globally available.
static constexpr const char* XLanguageName[] = {nullptr,
                                                "English",
                                                "Japanese",
                                                "German",
                                                "French",
                                                "Spanish",
                                                "Italian",
                                                "Korean",
                                                "Traditional Chinese",
                                                "Portuguese",
                                                "Simplified Chinese",
                                                "Polish",
                                                "Russian"};

static constexpr const char* XOnlineCountry[] = {nullptr,
                                                 "United Arab Emirates",
                                                 "Albania",
                                                 "Armenia",
                                                 "Argentina",
                                                 "Austria",
                                                 "Australia",
                                                 "Azerbaijan",
                                                 "Belgium",
                                                 "Bulgaria",
                                                 "Bahrain",
                                                 "Brunei Darussalam",
                                                 "Bolivia",
                                                 "Brazil",
                                                 "Belarus",
                                                 "Belize",
                                                 "Canada",
                                                 nullptr,
                                                 "Switzerland",
                                                 "Chile",
                                                 "China",
                                                 "Colombia",
                                                 "Costa Rica",
                                                 "Czech Republic",
                                                 "Germany",
                                                 "Denmark",
                                                 "Dominican Republic",
                                                 "Algeria",
                                                 "Ecuador",
                                                 "Estonia",
                                                 "Egypt",
                                                 "Spain",
                                                 "Finland",
                                                 "Faroe Islands",
                                                 "France",
                                                 "Great Britain",
                                                 "Georgia",
                                                 "Greece",
                                                 "Guatemala",
                                                 "Hong Kong",
                                                 "Honduras",
                                                 "Croatia",
                                                 "Hungary",
                                                 "Indonesia",
                                                 "Ireland",
                                                 "Israel",
                                                 "India",
                                                 "Iraq",
                                                 "Iran",
                                                 "Iceland",
                                                 "Italy",
                                                 "Jamaica",
                                                 "Jordan",
                                                 "Japan",
                                                 "Kenya",
                                                 "Kyrgyzstan",
                                                 "Korea",
                                                 "Kuwait",
                                                 "Kazakhstan",
                                                 "Lebanon",
                                                 "Liechtenstein",
                                                 "Lithuania",
                                                 "Luxembourg",
                                                 "Latvia",
                                                 "Libya",
                                                 "Morocco",
                                                 "Monaco",
                                                 "Macedonia",
                                                 "Mongolia",
                                                 "Macau",
                                                 "Maldives",
                                                 "Mexico",
                                                 "Malaysia",
                                                 "Nicaragua",
                                                 "Netherlands",
                                                 "Norway",
                                                 "New Zealand",
                                                 "Oman",
                                                 "Panama",
                                                 "Peru",
                                                 "Philippines",
                                                 "Pakistan",
                                                 "Poland",
                                                 "Puerto Rico",
                                                 "Portugal",
                                                 "Paraguay",
                                                 "Qatar",
                                                 "Romania",
                                                 "Russian Federation",
                                                 "Saudi Arabia",
                                                 "Sweden",
                                                 "Singapore",
                                                 "Slovenia",
                                                 "Slovak Republic",
                                                 nullptr,
                                                 "El Salvador",
                                                 "Syria",
                                                 "Thailand",
                                                 "Tunisia",
                                                 "Turkey",
                                                 "Trinidad And Tobago",
                                                 "Taiwan",
                                                 "Ukraine",
                                                 "United States",
                                                 "Uruguay",
                                                 "Uzbekistan",
                                                 "Venezuela",
                                                 "Viet Nam",
                                                 "Yemen",
                                                 "South Africa",
                                                 "Zimbabwe"};

static constexpr const char* AccountSubscription[] = {
    "None",  nullptr, nullptr, "Silver", nullptr,
    nullptr, "Gold",  nullptr, nullptr,  "Family"};

static constexpr const char* XGamerzoneName[] = {"None", "Recreation", "Pro",
                                                 "Family", "Underground"};

static constexpr const char* PreferredColorOptions[] = {
    "None", "Black",  "White", "Yellow", "Orange", "Pink",
    "Red",  "Purple", "Blue",  "Green",  "Brown",  "Silver"};

static constexpr const char* ControllerVibrationOptions[] = {"Off", nullptr,
                                                             nullptr, "On"};

static constexpr const char* ControlSensitivityOptions[] = {"Medium", "Low",
                                                            "High"};

static constexpr const char* GamerDifficultyOptions[] = {"Normal", "Easy",
                                                         "Hard"};

static constexpr const char* AutoAimOptions[] = {"Off", "On"};
static constexpr const char* AutoCenterOptions[] = {"Off", "On"};
static constexpr const char* MovementControlOptions[] = {"Left Thumbstick",
                                                         "Right Thumbstick"};
static constexpr const char* YAxisInversionOptions[] = {"Off", "On"};
static constexpr const char* TransmissionOptions[] = {"Automatic", "Manual"};
static constexpr const char* CameraLocationOptions[] = {"Behind", "In Front",
                                                        "Inside"};
static constexpr const char* BrakeControlOptions[] = {"Trigger", "Button"};
static constexpr const char* AcceleratorControlOptions[] = {"Trigger",
                                                            "Button"};

constexpr std::array<UserSettingId, 19> UserSettingsToLoad = {
    UserSettingId::XPROFILE_GAMER_TYPE,
    UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
    UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
    UserSettingId::XPROFILE_GAMERCARD_ZONE,
    UserSettingId::XPROFILE_GAMERCARD_REGION,
    UserSettingId::XPROFILE_GAMER_DIFFICULTY,
    UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
    UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
    UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
    UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
    UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
    UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
    UserSettingId::XPROFILE_GAMERCARD_MOTTO};

GamercardUI::GamercardUI(xe::ui::Window* window,
                         xe::ui::ImGuiDrawer* imgui_drawer,
                         KernelState* kernel_state, uint64_t xuid)
    : XamDialog(imgui_drawer),
      window_(window),
      kernel_state_(kernel_state),
      xuid_(xuid),
      is_signed_in_(kernel_state->xam_state()->GetUserProfile(xuid) !=
                    nullptr) {
  LoadGamercardInfo();
}

void GamercardUI::LoadStringSetting(UserSettingId setting_id, char* buffer) {
  const auto entry = xe::to_utf8(std::get<std::u16string>(
      gamercardOriginalValues_.gpd_settings[setting_id]));

  std::memcpy(buffer, entry.c_str(), entry.size());
}

void GamercardUI::LoadGamercardInfo() {
  const auto account_data =
      kernel_state()->xam_state()->profile_manager()->GetAccount(xuid_);

  std::memcpy(gamercardOriginalValues_.gamertag,
              account_data->GetGamertagString().c_str(),
              account_data->GetGamertagString().size());

  gamercardOriginalValues_.country = account_data->GetCountry();
  gamercardOriginalValues_.language = account_data->GetLanguage();

  gamercardOriginalValues_.is_live_enabled = account_data->IsLiveEnabled();

  const std::string online_xuid_hex =
      string_util::to_hex_string(account_data->GetOnlineXUID());

  std::memcpy(gamercardOriginalValues_.online_xuid, online_xuid_hex.c_str(),
              online_xuid_hex.size());

  std::memcpy(gamercardOriginalValues_.online_domain,
              account_data->GetOnlineDomain().data(),
              account_data->GetOnlineDomain().size());

  gamercardOriginalValues_.account_subscription_tier =
      account_data->GetSubscriptionTier();

  if (is_signed_in_) {
    // GPD settings to load
    for (const auto setting_id : UserSettingsToLoad) {
      LoadSetting(setting_id);
    }

    const auto gamer_icon =
        kernel_state()->xam_state()->GetUserProfile(xuid_)->GetProfileIcon(
            XTileType::kGamerTile);

    gamercardOriginalValues_.profile_icon.assign(gamer_icon.begin(),
                                                 gamer_icon.end());

    gamercardOriginalValues_.icon_texture =
        imgui_drawer()->LoadImGuiIcon(gamer_icon).release();

    LoadStringSetting(UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
                      gamercardOriginalValues_.gamer_name);

    LoadStringSetting(UserSettingId::XPROFILE_GAMERCARD_MOTTO,
                      gamercardOriginalValues_.gamer_motto);

    LoadStringSetting(UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
                      gamercardOriginalValues_.gamer_bio);
  }

  gamercardValues_ = gamercardOriginalValues_;
}

void GamercardUI::LoadSetting(UserSettingId setting_id) {
  const auto setting = kernel_state()->xam_state()->user_tracker()->GetSetting(
      kernel_state()->xam_state()->GetUserProfile(xuid_), kDashboardID,
      static_cast<uint32_t>(setting_id));

  if (!setting) {
    return;
  }

  gamercardOriginalValues_.gpd_settings[setting_id] =
      setting.value().get_host_data();
}

void GamercardUI::DrawInputTextBox(
    std::string label, char* buffer, size_t buffer_size, float alignment,
    std::function<bool(std::span<char>)> on_input_change) {
  ImGui::Text("%s", label.c_str());
  ImGui::SameLine(alignment);

  const ImVec2 input_field_pos = ImGui::GetCursorScreenPos();

  ImGui::InputText(fmt::format("###{}", label).c_str(), buffer, buffer_size);

  if (on_input_change) {
    if (!on_input_change({buffer, buffer_size})) {
      const ImVec2 item_size = ImGui::GetItemRectSize();
      const ImVec2 end_pos = ImVec2(input_field_pos.x + item_size.x,
                                    input_field_pos.y + item_size.y);

      ImDrawList* draw_list = ImGui::GetWindowDrawList();

      draw_list->AddRect(input_field_pos, end_pos, IM_COL32(255, 0, 0, 200),
                         0.0f, 0, 2.0f);
    }
  }
}

void GamercardUI::DrawSettingComboBox(UserSettingId setting_id,
                                      std::string label,
                                      const char* const items[], int item_count,
                                      float alignment) {
  ImGui::Text("%s:", label.c_str());
  ImGui::SameLine(alignment);

  if (gamercardValues_.gpd_settings.contains(setting_id)) {
    auto entry = &std::get<int32_t>(gamercardValues_.gpd_settings[setting_id]);

    ImGui::Combo(fmt::format("###{}", label).c_str(),
                 reinterpret_cast<int*>(entry), items, item_count);
  } else {
    ImGui::BeginDisabled();
    int empty = 0;
    ImGui::Combo(fmt::format("###{}", label).c_str(), &empty, items, 0);
    ImGui::EndDisabled();
  }
}

void GamercardUI::SelectNewIcon() {
  std::filesystem::path path;

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(xe::ui::FilePicker::Mode::kOpen);
  file_picker->set_type(xe::ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(false);
  file_picker->set_title("Select PNG Image");
  file_picker->set_extensions({{"PNG Image", "*.png"}});

  if (file_picker->Show(window_)) {
    auto selected_files = file_picker->selected_files();
    if (!selected_files.empty()) {
      path = selected_files[0];
    }

    if (IsFilePngImage(path)) {
      const auto res = GetImageResolution(path);

      if (res == kernel::xam::kProfileIconSizeSmall ||
          res == kernel::xam::kProfileIconSize) {
        gamercardValues_.profile_icon = ReadPngFromFile(path);
        gamercardValues_.icon_texture =
            imgui_drawer()
                ->LoadImGuiIcon(gamercardValues_.profile_icon)
                .release();
      }
    }
  }
}

void GamercardUI::DrawBaseSettings(ImGuiIO& io) {
  ImGui::SeparatorText("Profile Settings");

  DrawInputTextBox(
      "Gamertag:", gamercardValues_.gamertag,
      std::size(gamercardValues_.gamertag), leftSideTextObjectAlignment,
      [](std::span<const char> data) {
        return ProfileManager::IsGamertagValid(std::string(data.data()));
      });

  ImGui::BeginDisabled(!is_signed_in_);
  ImGui::BeginDisabled(kernel_state_->title_id());
  if (ImGui::ImageButton(
          "###ProfileIcon",
          reinterpret_cast<ImTextureID>(gamercardValues_.icon_texture),
          xe::ui::default_image_icon_size)) {
    SelectNewIcon();
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
    if (kernel_state_->title_id()) {
      ImGui::SetTooltip("Icon change is disabled when title is running.");
    } else {
      ImGui::SetTooltip(
          "Provide a PNG image with a resolution of 64x64. Icon will refresh "
          "after relog.");
    }
  }

  ImGui::Text("Gamer Name:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::InputText("###GamerName", gamercardValues_.gamer_name,
                   static_cast<int>(std::size(gamercardValues_.gamer_name)),
                   ImGuiInputTextFlags_ReadOnly);

  ImGui::Text("Gamer Motto:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::InputText("###GamerMotto", gamercardValues_.gamer_motto,
                   static_cast<int>(std::size(gamercardValues_.gamer_motto)),
                   ImGuiInputTextFlags_ReadOnly);

  ImGui::Text("Gamer Bio:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::InputTextMultiline(
      "###GamerBio", gamercardValues_.gamer_bio,
      static_cast<int>(std::size(gamercardValues_.gamer_bio)), ImVec2(),
      ImGuiInputTextFlags_ReadOnly);

  ImGui::EndDisabled();

  ImGui::Text("Language:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::Combo("###Language",
               reinterpret_cast<int*>(&gamercardValues_.language),
               XLanguageName, static_cast<int>(std::size(XLanguageName)));

  ImGui::Text("Country:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::Combo("###Country", reinterpret_cast<int*>(&gamercardValues_.country),
               XOnlineCountry, static_cast<int>(std::size(XOnlineCountry)));
}

void GamercardUI::DrawOnlineSettings(ImGuiIO& io) {
  ImGui::SeparatorText("Online Profile Settings");

  if (ImGui::Checkbox("Live Enabled", &gamercardValues_.is_live_enabled)) {
    // TODO: Add checks to decide if online XUID generation is required
  }

  ImGui::Text("Online XUID:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::InputText("###OnlineXUID", gamercardValues_.online_xuid,
                   std::size(gamercardValues_.online_xuid),
                   ImGuiInputTextFlags_ReadOnly);

  ImGui::Text("Online Domain:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::InputText("###OnlineDomain", gamercardValues_.online_domain,
                   std::size(gamercardValues_.online_domain),
                   ImGuiInputTextFlags_ReadOnly);

  ImGui::BeginDisabled(!gamercardValues_.is_live_enabled);
  DrawSettingComboBox(
      UserSettingId::XPROFILE_GAMERCARD_ZONE, "Gamer Zone", XGamerzoneName,
      static_cast<int>(std::size(XGamerzoneName)), leftSideTextObjectAlignment);

  ImGui::Text("Subscription Tier:");
  ImGui::SameLine(leftSideTextObjectAlignment);
  ImGui::Combo(
      "###Subscription",
      reinterpret_cast<int*>(&gamercardValues_.account_subscription_tier),
      AccountSubscription, static_cast<int>(std::size(AccountSubscription)));

  ImGui::EndDisabled();
}

void GamercardUI::DrawGpdSettings(ImGuiIO& io) {
  ImGui::SeparatorText("Game Settings");

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_DIFFICULTY, "Difficulty",
                      GamerDifficultyOptions,
                      static_cast<int>(std::size(GamerDifficultyOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
                      "Controller Vibration", ControllerVibrationOptions,
                      static_cast<int>(std::size(ControllerVibrationOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
                      "Control Sensitivity", ControlSensitivityOptions,
                      static_cast<int>(std::size(ControlSensitivityOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
                      "Favorite Color (First)", PreferredColorOptions,
                      static_cast<int>(std::size(PreferredColorOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
                      "Favorite Color (Second)", PreferredColorOptions,
                      static_cast<int>(std::size(PreferredColorOptions)),
                      rightSideTextObjectAlignment);

  ImGui::SeparatorText("Action Games Settings");

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
                      "Y-axis Inversion", YAxisInversionOptions,
                      static_cast<int>(std::size(YAxisInversionOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM, "Auto Aim",
                      AutoAimOptions,
                      static_cast<int>(std::size(AutoAimOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
                      "Auto Center", AutoCenterOptions,
                      static_cast<int>(std::size(AutoCenterOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
                      "Movement Control", MovementControlOptions,
                      static_cast<int>(std::size(MovementControlOptions)),
                      rightSideTextObjectAlignment);

  ImGui::SeparatorText("Racing Games Settings");

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
                      "Transmission", TransmissionOptions,
                      static_cast<int>(std::size(TransmissionOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
                      "Camera Location", CameraLocationOptions,
                      static_cast<int>(std::size(CameraLocationOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
                      "Brake Control", BrakeControlOptions,
                      static_cast<int>(std::size(BrakeControlOptions)),
                      rightSideTextObjectAlignment);

  DrawSettingComboBox(UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
                      "Accelerator Control", AcceleratorControlOptions,
                      static_cast<int>(std::size(AcceleratorControlOptions)),
                      rightSideTextObjectAlignment);
}

void GamercardUI::OnDraw(ImGuiIO& io) {
  if (!has_opened_) {
    ImGui::OpenPopup(fmt::format("{}'s Gamercard",
                                 std::string(gamercardOriginalValues_.gamertag))
                         .c_str());
    has_opened_ = true;
  }

  bool dialog_open = true;
  if (!ImGui::BeginPopupModal(
          fmt::format("{}'s Gamercard",
                      std::string(gamercardOriginalValues_.gamertag))
              .c_str(),
          &dialog_open,
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_HorizontalScrollbar)) {
    Close();
    return;
  }

  if (ImGui::BeginTable("###GamercardTable", 2)) {
    ImGui::TableSetupColumn("###Label", ImGuiTableColumnFlags_WidthFixed,
                            350.0f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    DrawBaseSettings(io);
    DrawOnlineSettings(io);
    ImGui::TableNextColumn();
    DrawGpdSettings(io);

    ImGui::EndTable();
  }

  ImGui::NewLine();

  const bool is_valid_gamertag =
      ProfileManager::IsGamertagValid(std::string(gamercardValues_.gamertag));

  ImGui::BeginDisabled(!is_valid_gamertag);
  if (ImGui::Button("Save")) {
    SaveProfileIcon();
    SaveSettings();
    SaveAccountData();
    dialog_open = false;
  }
  if (!is_valid_gamertag) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
      ImGui::SetTooltip("Saving disabled! Invalid gamertag provided.");
  }

  ImGui::EndDisabled();

  ImGui::SameLine();

  if (ImGui::Button("Cancel")) {
    dialog_open = false;
  }

  if (!dialog_open) {
    ImGui::CloseCurrentPopup();
    Close();
    ImGui::EndPopup();
    return;
  }
  ImGui::EndPopup();
}

void GamercardUI::SaveAccountData() {
  const auto account_original =
      *kernel_state()->xam_state()->profile_manager()->GetAccount(xuid_);
  auto account = account_original;

  account.SetCountry(gamercardValues_.country);
  account.SetLanguage(gamercardValues_.language);
  account.SetSubscriptionTier(gamercardValues_.account_subscription_tier);
  account.ToggleLiveFlag(gamercardValues_.is_live_enabled);

  std::u16string gamertag =
      xe::to_utf16(std::string(gamercardValues_.gamertag));
  string_util::copy_truncating(account.gamertag, gamertag,
                               std::size(account.gamertag));

  if (std::memcmp(&account, &account_original, sizeof(X_XAMACCOUNTINFO)) != 0) {
    if (!is_signed_in_) {
      kernel_state()->xam_state()->profile_manager()->MountProfile(xuid_);
    }

    kernel_state()->xam_state()->profile_manager()->UpdateAccount(xuid_,
                                                                  &account);

    if (!is_signed_in_) {
      kernel_state()->xam_state()->profile_manager()->DismountProfile(xuid_);
    }
  }
}

void GamercardUI::SaveProfileIcon() {
  if (gamercardValues_.profile_icon == gamercardOriginalValues_.profile_icon) {
    return;
  }

  kernel_state()->xam_state()->user_tracker()->UpdateUserIcon(
      xuid_, {gamercardValues_.profile_icon.data(),
              gamercardValues_.profile_icon.size()});
}

void GamercardUI::SaveSettings() {
  // First check all GPD embedded settings.
  for (const auto& [id, setting] : gamercardValues_.gpd_settings) {
    // No change was made to the setting?
    if (gamercardOriginalValues_.gpd_settings[id] == setting) {
      continue;
    }

    UserSetting updated_setting(id, setting);
    kernel_state()->xam_state()->user_tracker()->UpsertSetting(
        xuid_, kDashboardID, &updated_setting);
  }

  const uint32_t user_index = kernel_state()
                                  ->xam_state()
                                  ->profile_manager()
                                  ->GetUserIndexAssignedToProfile(xuid_);

  if (user_index != XUserIndexAny) {
    // TODO: Fix it to update only changed player
    kernel_state_->BroadcastNotification(
        kXNotificationSystemProfileSettingChanged, 0xF);
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
