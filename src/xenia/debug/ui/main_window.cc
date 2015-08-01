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
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace debug {
namespace ui {

using namespace xe::debug::client::xdp;

using xe::ui::MenuItem;

const std::wstring kBaseTitle = L"xenia debugger";

MainWindow::MainWindow(Application* app) : app_(app) {}

MainWindow::~MainWindow() = default;

bool MainWindow::Initialize() {
  client_ = app_->client();

  window_ = xe::ui::Window::Create(app()->loop(), kBaseTitle);
  if (!window_) {
    return false;
  }
  window_->Initialize();
  window_->set_context(xe::ui::gl::GLContext::Create(window_.get()));
  window_->MakeReady();

  window_->on_closed.AddListener(std::bind(&MainWindow::OnClose, this));

  window_->on_key_down.AddListener([this](xe::ui::KeyEvent& e) {
    bool handled = true;
    switch (e.key_code()) {
      case 0x1B: {  // VK_ESCAPE
                    // Allow users to escape fullscreen (but not enter it).
        if (window_->is_fullscreen()) {
          window_->ToggleFullscreen(false);
        }
      } break;

      case 0x70: {  // VK_F1
        LaunchBrowser("http://xenia.jp");
      } break;

      default: { handled = false; } break;
    }
    e.set_handled(handled);
  });

  // Main menu.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&File");
  {
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kString, L"E&xit",
                                         L"Alt+F4",
                                         [this]() { window_->Close(); }));
  }
  main_menu->AddChild(std::move(file_menu));

  // Help menu.
  auto help_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&Help");
  {
    help_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&Website...", L"F1",
                         []() { LaunchBrowser("http://xenia.jp"); }));
    help_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&About...",
                         []() { LaunchBrowser("http://xenia.jp/about/"); }));
  }
  main_menu->AddChild(std::move(help_menu));

  window_->set_main_menu(std::move(main_menu));

  window_->Resize(1440, 1200);

  BuildUI();

  return true;
}

void MainWindow::BuildUI() {
  using namespace el::dsl;
  el::AnimationBlocker animation_blocker;

  auto root_element = window_->root_element();
  form_ = std::make_unique<el::Form>();
  form_->set_settings(el::FormSettings::kFullScreen);
  root_element->AddChild(form_.get());

  auto root_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(
              LayoutBoxNode()
                  .id("toolbar_box")
                  .gravity(Gravity::kTop | Gravity::kLeftRight)
                  .distribution(LayoutDistribution::kGravity)
                  .distribution_position(LayoutDistributionPosition::kLeftTop)
                  .child(ButtonNode("Pause").id("pause_button"))
                  .child(ButtonNode("Continue").id("resume_button")))
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

  form_->LoadNodeTree(root_node);
  form_->GetElementsById({
      {TBIDC("split_container"), &ui_.split_container},
      {TBIDC("toolbar_box"), &ui_.toolbar_box},
      {TBIDC("tab_container"), &ui_.tab_container},
  });

  handler_ = std::make_unique<el::EventHandler>(form_.get());
  handler_->Listen(el::EventType::kClick, TBIDC("pause_button"),
                   [this](const el::Event& ev) {
                     client_->Interrupt();
                     return true;
                   });
  handler_->Listen(el::EventType::kClick, TBIDC("resume_button"),
                   [this](const el::Event& ev) {
                     client_->Continue();
                     return true;
                   });

  system()->on_execution_state_changed.AddListener(
      [this]() { UpdateElementState(); });

  ui_.tab_container->tab_bar()->LoadNodeTree(ButtonNode(cpu_view_.name()));
  ui_.tab_container->content_root()->AddChild(cpu_view_.BuildUI());
  cpu_view_.Setup(Application::current()->client());

  ui_.tab_container->tab_bar()->LoadNodeTree(ButtonNode(gpu_view_.name()));
  ui_.tab_container->content_root()->AddChild(gpu_view_.BuildUI());
  gpu_view_.Setup(Application::current()->client());

  UpdateElementState();

  el::util::ShowDebugInfoSettingsForm(root_element);
}

void MainWindow::UpdateElementState() {
  bool is_running = client_->execution_state() == ExecutionState::kRunning;

  el::TabContainer* tab_container;
  el::Button* pause_button;
  el::Button* resume_button;
  form_->GetElementsById({
      {TBIDC("tab_container"), &tab_container},
      {TBIDC("pause_button"), &pause_button},
      {TBIDC("resume_button"), &resume_button},
  });
  tab_container->set_enabled(!is_running);
  pause_button->set_enabled(is_running);
  resume_button->set_enabled(!is_running);
}

void MainWindow::OnClose() { app_->Quit(); }

}  // namespace ui
}  // namespace debug
}  // namespace xe
