/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UI_TITLE_INFO_UI_H_
#define XENIA_KERNEL_XAM_UI_TITLE_INFO_UI_H_

#include "xenia/kernel/xam/xam_ui.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

class TitleListUI final : public XamDialog {
 public:
  TitleListUI(xe::ui::ImGuiDrawer* imgui_drawer, const ImVec2 drawing_position,
              const UserProfile* profile);

 private:
  ~TitleListUI();

  void OnDraw(ImGuiIO& io) override;

  void LoadProfileTitleList(xe::ui::ImGuiDrawer* imgui_drawer,
                            const UserProfile* profile);
  void DrawTitleEntry(ImGuiIO& io, TitleInfo& entry);

  static constexpr uint8_t title_name_filter_size = 15;

  std::string dialog_name_ = "";
  char title_name_filter_[title_name_filter_size] = "";
  uint32_t selected_title_ = 0;
  const ImVec2 drawing_position_ = {};

  const UserProfile* profile_;
  const ProfileManager* profile_manager_;

  std::map<uint32_t, std::unique_ptr<xe::ui::ImmediateTexture>> title_icon;
  std::vector<TitleInfo> info_;
};

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
