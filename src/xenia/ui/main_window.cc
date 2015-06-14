/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/main_window.h"

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/emulator.h"
#include "xenia/profiling.h"

namespace xe {
namespace ui {

const std::wstring kBaseTitle = L"xenia";

MainWindow::MainWindow(Emulator* emulator)
    : PlatformWindow(kBaseTitle),
      emulator_(emulator),
      main_menu_(MenuItem::Type::kNormal),
      fullscreen_(false) {}

MainWindow::~MainWindow() = default;

void MainWindow::Start() {
  xe::threading::Fence fence;

  loop_.Post([&]() {
    xe::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");

    if (!Initialize()) {
      XEFATAL("Failed to initialize main window");
      exit(1);
    }

    fence.Signal();
  });

  fence.Wait();
}

bool MainWindow::Initialize() {
  if (!PlatformWindow::Initialize()) {
    return false;
  }
  
  UpdateTitle();
  on_key_down.AddListener([this](KeyEvent& e) {
    bool handled = true;
    switch (e.key_code()) {
      case 0x73: {  // VK_F4
        emulator()->graphics_system()->RequestFrameTrace();
        break;
      }
      case 0x74: {  // VK_F5
        emulator()->graphics_system()->ClearCaches();
        break;
      }
      case 0x75: { // VK_F6
        ToggleFullscreen();
        break;
      }
      case 0x1B: { // VK_ESCAPE
        // Allow users to escape fullscreen (but not enter it)
        if (fullscreen_) {
          ToggleFullscreen();
        }
      }
      case 0x6D: {  // numpad minus
        Clock::set_guest_time_scalar(Clock::guest_time_scalar() / 2.0);
        UpdateTitle();
        break;
      }
      case 0x6B: {  // numpad plus
        Clock::set_guest_time_scalar(Clock::guest_time_scalar() * 2.0);
        UpdateTitle();
        break;
      }
      case 0x0D: {  // numpad enter
        Clock::set_guest_time_scalar(1.0);
        UpdateTitle();
        break;
      }
      default: {
        handled = false;
        break;
      }
    }
    e.set_handled(handled);
  });

  // Main menu
  // FIXME: This code is really messy.
  auto file = std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&File");
  file->AddChild(std::make_unique<PlatformMenu>(
      MenuItem::Type::kString, Commands::IDC_FILE_OPEN, L"&Open"));

  main_menu_.AddChild(std::move(file));

  // Window submenu
  auto window =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&Window");
  window->AddChild(std::make_unique<PlatformMenu>(
      MenuItem::Type::kString, Commands::IDC_WINDOW_FULLSCREEN,
      L"Fullscreen\tF6"));

  main_menu_.AddChild(std::move(window));

  SetMenu(&main_menu_);

  Resize(1280, 720);

  return true;
}

void MainWindow::UpdateTitle() {
  std::wstring title(kBaseTitle);
  if (Clock::guest_time_scalar() != 1.0) {
    title += L" (@";
    title += xe::to_wstring(std::to_string(Clock::guest_time_scalar()));
    title += L"x)";
  }
  set_title(title);
}

void MainWindow::ToggleFullscreen() {
  fullscreen_ = !fullscreen_;
  SetFullscreen(fullscreen_);
}

void MainWindow::OnClose() {
  loop_.Quit();

  // TODO(benvanik): proper exit.
  XELOGI("User-initiated death!");
  exit(1);
}

void MainWindow::OnCommand(int id) {
  switch (id) {
    // TODO: Setup delegates to MenuItems so we don't have to do this
    case IDC_WINDOW_FULLSCREEN: {
      ToggleFullscreen();
      break;
    }
  }
}

X_STATUS MainWindow::LaunchPath(std::wstring path) {
  X_STATUS result;

  // Launch based on file type.
  // This is a silly guess based on file extension.
  // NOTE: this blocks!
  auto file_system_type = emulator_->file_system()->InferType(path);
  switch (file_system_type) {
    case kernel::fs::FileSystemType::STFS_TITLE:
      result = emulator_->LaunchSTFSTitle(path);
      break;
    case kernel::fs::FileSystemType::XEX_FILE:
      result = emulator_->LaunchXexFile(path);
      break;
    case kernel::fs::FileSystemType::DISC_IMAGE:
      result = emulator_->LaunchDiscImage(path);
      break;
  }

  return result;
}

}  // namespace ui
}  // namespace xe
