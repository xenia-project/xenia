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
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/emulator.h"
#include "xenia/profiling.h"

namespace xe {
namespace ui {

enum Commands {
  IDC_FILE_EXIT,

  IDC_CPU_TIME_SCALAR_RESET,
  IDC_CPU_TIME_SCALAR_HALF,
  IDC_CPU_TIME_SCALAR_DOUBLE,

  IDC_CPU_PROFILER_TOGGLE_DISPLAY,
  IDC_CPU_PROFILER_TOGGLE_PAUSE,

  IDC_GPU_TRACE_FRAME,
  IDC_GPU_CLEAR_CACHES,

  IDC_WINDOW_FULLSCREEN,

  IDC_HELP_WEBSITE,
  IDC_HELP_ABOUT,
};

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
      case 0x0D: {  // numpad enter
        OnCommand(Commands::IDC_CPU_TIME_SCALAR_RESET);
      } break;
      case 0x6D: {  // numpad minus
        OnCommand(Commands::IDC_CPU_TIME_SCALAR_HALF);
      } break;
      case 0x6B: {  // numpad plus
        OnCommand(Commands::IDC_CPU_TIME_SCALAR_DOUBLE);
      } break;

      case 0x73: {  // VK_F4
        OnCommand(Commands::IDC_GPU_TRACE_FRAME);
      } break;
      case 0x74: {  // VK_F5
        OnCommand(Commands::IDC_GPU_CLEAR_CACHES);
      } break;

      case 0x7A: {  // VK_F11
        OnCommand(Commands::IDC_WINDOW_FULLSCREEN);
      } break;
      case 0x1B: {  // VK_ESCAPE
                    // Allow users to escape fullscreen (but not enter it).
        if (fullscreen_) {
          ToggleFullscreen();
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
  // FIXME: This code is really messy.
  auto file_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&File");
  {
    file_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_FILE_EXIT, L"E&xit", L"Alt+F4"));
  }
  main_menu_.AddChild(std::move(file_menu));

  // CPU menu.
  auto cpu_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&CPU");
  {
    cpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_CPU_TIME_SCALAR_RESET,
        L"&Reset Time Scalar", L"Numpad Enter"));
    cpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_CPU_TIME_SCALAR_HALF,
        L"Time Scalar /= 2", L"Numpad -"));
    cpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_CPU_TIME_SCALAR_DOUBLE,
        L"Time Scalar *= 2", L"Numpad +"));
  }
  cpu_menu->AddChild(
      std::make_unique<PlatformMenu>(MenuItem::Type::kSeparator));
  {
    cpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_CPU_PROFILER_TOGGLE_DISPLAY,
        L"Toggle Profiler &Display", L"Tab"));
    cpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_CPU_PROFILER_TOGGLE_PAUSE,
        L"&Pause/Resume Profiler", L"`"));
  }
  main_menu_.AddChild(std::move(cpu_menu));

  // GPU menu.
  auto gpu_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&GPU");
  {
    gpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_GPU_TRACE_FRAME, L"&Trace Frame",
        L"F4"));
  }
  gpu_menu->AddChild(
      std::make_unique<PlatformMenu>(MenuItem::Type::kSeparator));
  {
    gpu_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_GPU_CLEAR_CACHES,
        L"&Clear Caches", L"F5"));
  }
  main_menu_.AddChild(std::move(gpu_menu));

  // Window menu.
  auto window_menu =
      std::make_unique<PlatformMenu>(MenuItem::Type::kPopup, L"&Window");
  {
    window_menu->AddChild(std::make_unique<PlatformMenu>(
        MenuItem::Type::kString, Commands::IDC_WINDOW_FULLSCREEN,
        L"&Fullscreen", L"F11"));
  }
  main_menu_.AddChild(std::move(window_menu));

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
    case IDC_FILE_EXIT: {
      Close();
    } break;

    case IDC_CPU_TIME_SCALAR_RESET: {
      Clock::set_guest_time_scalar(1.0);
      UpdateTitle();
    } break;
    case IDC_CPU_TIME_SCALAR_HALF: {
      Clock::set_guest_time_scalar(Clock::guest_time_scalar() / 2.0);
      UpdateTitle();
    } break;
    case IDC_CPU_TIME_SCALAR_DOUBLE: {
      Clock::set_guest_time_scalar(Clock::guest_time_scalar() * 2.0);
      UpdateTitle();
    } break;
    case IDC_CPU_PROFILER_TOGGLE_DISPLAY: {
      Profiler::ToggleDisplay();
    } break;
    case IDC_CPU_PROFILER_TOGGLE_PAUSE: {
      Profiler::TogglePause();
    } break;

    case IDC_GPU_TRACE_FRAME: {
      emulator()->graphics_system()->RequestFrameTrace();
    } break;
    case IDC_GPU_CLEAR_CACHES: {
      emulator()->graphics_system()->ClearCaches();
    } break;

    case IDC_WINDOW_FULLSCREEN: {
      ToggleFullscreen();
    } break;

    case IDC_HELP_WEBSITE: {
      LaunchBrowser("http://xenia.jp");
    } break;
    case IDC_HELP_ABOUT: {
      LaunchBrowser("http://xenia.jp/about/");
    } break;
  }
}

X_STATUS MainWindow::LaunchPath(std::wstring path) {
  X_STATUS result;

  // Launch based on file type.
  // This is a silly guess based on file extension.
  // NOTE: this blocks!
  auto file_system_type = emulator_->file_system()->InferType(path);
  switch (file_system_type) {
    case vfs::FileSystemType::STFS_TITLE:
      result = emulator_->LaunchSTFSTitle(path);
      break;
    case vfs::FileSystemType::XEX_FILE:
      result = emulator_->LaunchXexFile(path);
      break;
    case vfs::FileSystemType::DISC_IMAGE:
      result = emulator_->LaunchDiscImage(path);
      break;
  }

  return result;
}

}  // namespace ui
}  // namespace xe
