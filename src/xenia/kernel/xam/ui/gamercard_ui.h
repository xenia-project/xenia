/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_GAMERCARD_UI_H_
#define XENIA_KERNEL_XAM_UI_GAMERCARD_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

struct GamercardSettings {
  // Account settings
  char gamertag[16];
  xe::XOnlineCountry country;
  xe::XLanguage language;
  bool is_live_enabled;
  char online_xuid[0x11];
  char online_domain[0x14];
  X_XAMACCOUNTINFO::AccountSubscriptionTier account_subscription_tier;

  // GPD settings
  std::map<UserSettingId, UserDataTypes> gpd_settings;

  // GPD string buffers
  char gamer_name[0x104];
  char gamer_motto[0x2C];
  char gamer_bio[kMaxUserDataSize];

  // Other
  std::vector<uint8_t> profile_icon;
  // Immediate Texture?
  xe::ui::ImmediateTexture* icon_texture;
};

class GamercardUI final : public XamDialog {
 public:
  GamercardUI(xe::ui::Window* window, xe::ui::ImGuiDrawer* imgui_drawer,
              KernelState* kernel_state, uint64_t xuid);

  ~GamercardUI() = default;

 private:
  void OnDraw(ImGuiIO& io) override;
  void DrawBaseSettings(ImGuiIO& io);
  void DrawOnlineSettings(ImGuiIO& io);
  void DrawGpdSettings(ImGuiIO& io);

  void LoadGamercardInfo();
  void LoadSetting(UserSettingId setting_id);
  void LoadStringSetting(UserSettingId setting_id, char* buffer);
  void SaveSettings();
  void SaveAccountData();
  void SaveProfileIcon();

  void DrawSettingComboBox(UserSettingId setting_id, std::string label,
                           const char* const items[], int item_count,
                           float alignment);

  void DrawInputTextBox(
      std::string label, char* buffer, size_t buffer_size, float alignment,
      std::function<bool(std::span<char>)> on_input_change = {});

  void SelectNewIcon();

  const uint64_t xuid_ = 0;
  const bool is_signed_in_ = false;
  const bool is_valid_gamertag_ = true;

  bool has_opened_ = false;
  KernelState* kernel_state_;
  xe::ui::Window* window_;

  // We're storing OG and current values to compare at the end and send what was
  // changed.
  GamercardSettings gamercardOriginalValues_ = {};
  GamercardSettings gamercardValues_ = {};
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
