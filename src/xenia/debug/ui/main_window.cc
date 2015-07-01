/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/main_window.h"

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/profiling.h"

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

MainWindow::MainWindow()
    : PlatformWindow(&loop_, kBaseTitle), main_menu_(MenuItem::Type::kNormal) {}

MainWindow::~MainWindow() = default;

std::unique_ptr<MainWindow> MainWindow::Create() {
  std::unique_ptr<MainWindow> main_window(new MainWindow());

  xe::threading::Fence fence;

  main_window->loop()->Post([&main_window, &fence]() {
    xe::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");

    if (!main_window->Initialize()) {
      XEFATAL("Failed to initialize main window");
      exit(1);
    }

    fence.Signal();
  });

  fence.Wait();

  return main_window;
}

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
  control_ = std::make_unique<xe::ui::gl::WGLControl>(loop());
  AddChild(control_.get());

  Resize(1440, 1200);

  return true;
}

void MainWindow::OnClose() {
  loop_.Quit();

  // TODO(benvanik): proper exit.
  XELOGI("User-initiated death!");
  exit(1);
}

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
