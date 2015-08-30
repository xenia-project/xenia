/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/emulator_window.h"

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/emulator.h"
#include "xenia/profiling.h"

namespace xe {
namespace app {

using xe::ui::KeyEvent;
using xe::ui::MenuItem;
using xe::ui::MouseEvent;
using xe::ui::UIEvent;

const std::wstring kBaseTitle = L"xenia";

EmulatorWindow::EmulatorWindow(Emulator* emulator)
    : emulator_(emulator),
      loop_(ui::Loop::Create()),
      window_(ui::Window::Create(loop_.get(), kBaseTitle)) {}

EmulatorWindow::~EmulatorWindow() {
  loop_->PostSynchronous([this]() { window_.reset(); });
}

std::unique_ptr<EmulatorWindow> EmulatorWindow::Create(Emulator* emulator) {
  std::unique_ptr<EmulatorWindow> emulator_window(new EmulatorWindow(emulator));

  emulator_window->loop()->PostSynchronous([&emulator_window]() {
    xe::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");

    if (!emulator_window->Initialize()) {
      xe::FatalError("Failed to initialize main window");
      return;
    }
  });

  return emulator_window;
}

bool EmulatorWindow::Initialize() {
  if (!window_->Initialize()) {
    XELOGE("Failed to initialize platform window");
    return false;
  }

  UpdateTitle();

  window_->on_closed.AddListener([this](UIEvent* e) {
    loop_->Quit();

    // TODO(benvanik): proper exit.
    XELOGI("User-initiated death!");
    exit(1);
  });
  loop_->on_quit.AddListener([this](UIEvent* e) { window_.reset(); });

  window_->on_key_down.AddListener([this](KeyEvent* e) {
    bool handled = true;
    switch (e->key_code()) {
      case 0x6A: {  // numpad *
        CpuTimeScalarReset();
      } break;
      case 0x6D: {  // numpad minus
        CpuTimeScalarSetHalf();
      } break;
      case 0x6B: {  // numpad plus
        CpuTimeScalarSetDouble();
      } break;

      case 0x72: {  // F3
        Profiler::ToggleDisplay();
      } break;

      case 0x73: {  // VK_F4
        GpuTraceFrame();
      } break;
      case 0x74: {  // VK_F5
        GpuClearCaches();
      } break;

      case 0x7A: {  // VK_F11
        ToggleFullscreen();
      } break;
      case 0x1B: {  // VK_ESCAPE
                    // Allow users to escape fullscreen (but not enter it).
        if (window_->is_fullscreen()) {
          window_->ToggleFullscreen(false);
        } else {
          handled = false;
        }
      } break;

      case 0x70: {  // VK_F1
        ShowHelpWebsite();
      } break;

      default: { handled = false; } break;
    }
    e->set_handled(handled);
  });

  // Main menu.
  // FIXME: This code is really messy.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&File");
  {
    file_menu->AddChild(MenuItem::Create(MenuItem::Type::kString, L"E&xit",
                                         L"Alt+F4",
                                         [this]() { window_->Close(); }));
  }
  main_menu->AddChild(std::move(file_menu));

  // CPU menu.
  auto cpu_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&CPU");
  {
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, L"&Reset Time Scalar", L"Numpad *",
        std::bind(&EmulatorWindow::CpuTimeScalarReset, this)));
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, L"Time Scalar /= 2", L"Numpad -",
        std::bind(&EmulatorWindow::CpuTimeScalarSetHalf, this)));
    cpu_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, L"Time Scalar *= 2", L"Numpad +",
        std::bind(&EmulatorWindow::CpuTimeScalarSetDouble, this)));
  }
  cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                        L"Toggle Profiler &Display", L"F3",
                                        []() { Profiler::ToggleDisplay(); }));
    cpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kString,
                                        L"&Pause/Resume Profiler", L"`",
                                        []() { Profiler::TogglePause(); }));
  }
  main_menu->AddChild(std::move(cpu_menu));

  // GPU menu.
  auto gpu_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&GPU");
  {
    gpu_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&Trace Frame", L"F4",
                         std::bind(&EmulatorWindow::GpuTraceFrame, this)));
  }
  gpu_menu->AddChild(MenuItem::Create(MenuItem::Type::kSeparator));
  {
    gpu_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&Clear Caches", L"F5",
                         std::bind(&EmulatorWindow::GpuClearCaches, this)));
  }
  main_menu->AddChild(std::move(gpu_menu));

  // Window menu.
  auto window_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&Window");
  {
    window_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&Fullscreen", L"F11",
                         std::bind(&EmulatorWindow::ToggleFullscreen, this)));
  }
  main_menu->AddChild(std::move(window_menu));

  // Help menu.
  auto help_menu = MenuItem::Create(MenuItem::Type::kPopup, L"&Help");
  {
    help_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, L"&Website...", L"F1",
                         std::bind(&EmulatorWindow::ShowHelpWebsite, this)));
    help_menu->AddChild(MenuItem::Create(
        MenuItem::Type::kString, L"&About...",
        [this]() { LaunchBrowser("http://xenia.jp/about/"); }));
  }
  main_menu->AddChild(std::move(help_menu));

  window_->set_main_menu(std::move(main_menu));

  window_->Resize(1280, 720);

  return true;
}

void EmulatorWindow::CpuTimeScalarReset() {
  Clock::set_guest_time_scalar(1.0);
  UpdateTitle();
}

void EmulatorWindow::CpuTimeScalarSetHalf() {
  Clock::set_guest_time_scalar(Clock::guest_time_scalar() / 2.0);
  UpdateTitle();
}

void EmulatorWindow::CpuTimeScalarSetDouble() {
  Clock::set_guest_time_scalar(Clock::guest_time_scalar() * 2.0);
  UpdateTitle();
}

void EmulatorWindow::GpuTraceFrame() {
  emulator()->graphics_system()->RequestFrameTrace();
}

void EmulatorWindow::GpuClearCaches() {
  emulator()->graphics_system()->ClearCaches();
}

void EmulatorWindow::ToggleFullscreen() {
  window_->ToggleFullscreen(!window_->is_fullscreen());
}

void EmulatorWindow::ShowHelpWebsite() { LaunchBrowser("http://xenia.jp"); }

void EmulatorWindow::UpdateTitle() {
  std::wstring title(kBaseTitle);
  if (Clock::guest_time_scalar() != 1.0) {
    title += L" (@";
    title += xe::to_wstring(std::to_string(Clock::guest_time_scalar()));
    title += L"x)";
  }
  window_->set_title(title);
}

}  // namespace app
}  // namespace xe
