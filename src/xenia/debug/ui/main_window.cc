/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/main_window.h"

#include "el/animation_manager.h"
#include "el/util/debug.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"

// TODO(benvanik): platform based.
#include "xenia/ui/gl/wgl_elemental_control.h"

namespace xe {
namespace debug {
namespace ui {

using xe::ui::MenuItem;
using xe::ui::PlatformMenu;
using xe::ui::PlatformWindow;

enum Commands {
  IDC_FILE_EXIT,

  IDC_HELP_WEBSITE,
  IDC_HELP_ABOUT,
};

const std::wstring kBaseTitle = L"xenia debugger";

MainWindow::MainWindow(Application* app)
    : PlatformWindow(app->loop(), kBaseTitle),
      app_(app),
      main_menu_(MenuItem::Type::kNormal) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Initialize() {
  if (!PlatformWindow::Initialize()) {
    return false;
  }

  on_key_down.AddListener([this](xe::ui::KeyEvent& e) {
    bool handled = true;
    switch (e.key_code()) {
      case 0x1B: {  // VK_ESCAPE
                    // Allow users to escape fullscreen (but not enter it).
        if (is_fullscreen()) {
          ToggleFullscreen(false);
        }
      } break;

      case 0x70: {  // VK_F1
        OnCommand(Commands::IDC_HELP_WEBSITE);
      } break;

      default: { handled = false; } break;
    }
    e.set_handled(handled);
  });

  // Main menu.
  auto file_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&File");
  {
    file_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_FILE_EXIT, L"E&xit", L"Alt+F4"));
  }
  main_menu_.AddChild(std::move(file_menu));

  // Help menu.
  auto help_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&Help");
  {
    help_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_HELP_WEBSITE, L"&Website...",
        L"F1"));
    help_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_HELP_ABOUT, L"&About..."));
  }
  main_menu_.AddChild(std::move(help_menu));

  set_menu(&main_menu_);

  // Setup the GL control that actually does the drawing.
  // We run here in the loop and only touch it (and its context) on this
  // thread. That means some sync-fu when we want to swap.
  control_ = std::make_unique<xe::ui::gl::WGLElementalControl>(loop());
  AddChild(control_.get());

  Resize(1440, 1200);

  BuildUI();

  return true;
}

void MainWindow::BuildUI() {
  using namespace el::dsl;
  el::AnimationBlocker animation_blocker;

  auto root_element = control_->root_element();
  window_ = std::make_unique<el::Window>();
  window_->set_settings(el::WindowSettings::kFullScreen);
  root_element->AddChild(window_.get());

  auto root_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(LayoutBoxNode()
                     .id("toolbar_box")
                     .gravity(Gravity::kTop | Gravity::kLeftRight)
                     .distribution(LayoutDistribution::kAvailable)
                     .child(LabelNode("toolbar")))
          .child(
              SplitContainerNode()
                  .id("split_container")
                  .gravity(Gravity::kAll)
                  .axis(Axis::kX)
                  .fixed_pane(FixedPane::kSecond)
                  .min(128)
                  .value(250)
                  .pane(TabContainerNode()
                            .id("tab_container")
                            .gravity(Gravity::kAll)
                            .align(Align::kTop))
                  .pane(LayoutBoxNode().id("log_box").gravity(Gravity::kAll)));

  window_->LoadNodeTree(root_node);
  window_->GetElementsById({
      {TBIDC("split_container"), &ui_.split_container},
      {TBIDC("toolbar_box"), &ui_.toolbar_box},
      {TBIDC("tab_container"), &ui_.tab_container},
  });

  ui_.tab_container->tab_bar()->LoadNodeTree(ButtonNode(cpu_view_.name()));
  ui_.tab_container->content_root()->AddChild(cpu_view_.BuildUI());

  ui_.tab_container->tab_bar()->LoadNodeTree(ButtonNode(gpu_view_.name()));
  ui_.tab_container->content_root()->AddChild(gpu_view_.BuildUI());

  el::util::ShowDebugInfoSettingsWindow(root_element);
}

void MainWindow::OnClose() { app_->Quit(); }

void MainWindow::OnCommand(int id) {
  switch (id) {
    case IDC_FILE_EXIT: {
      Close();
    } break;

    case IDC_HELP_WEBSITE: {
      LaunchBrowser("http://xenia.jp");
    } break;
    case IDC_HELP_ABOUT: {
      LaunchBrowser("http://xenia.jp/about/");
    } break;
  }
}

}  // namespace ui
}  // namespace debug
}  // namespace xe
