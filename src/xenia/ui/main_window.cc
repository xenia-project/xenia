/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/main_window.h"

#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/emulator.h"
#include "xenia/profiling.h"

namespace xe {
namespace ui {

MainWindow::MainWindow(Emulator* emulator)
    : PlatformWindow(L"xenia"), emulator_(emulator) {}

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
  Resize(1280, 720);
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
      default: {
        handled = false;
        break;
      }
    }
    e.set_handled(handled);
  });
  return true;
}

void MainWindow::OnClose() {
  loop_.Quit();

  // TODO(benvanik): proper exit.
  XELOGI("User-initiated death!");
  exit(1);
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
