/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/main_window.h"

#include "poly/logging.h"
#include "poly/threading.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/emulator.h"

namespace xe {
namespace ui {

MainWindow::MainWindow(Emulator* emulator)
    : PlatformWindow(L"xenia"), emulator_(emulator) {}

MainWindow::~MainWindow() = default;

void MainWindow::Start() {
  poly::threading::Fence fence;

  loop_.Post([&]() {
    poly::threading::set_name("Win32 Loop");
    xe::Profiler::ThreadEnter("Win32 Loop");

    if (!Initialize()) {
      PFATAL("Failed to initialize main window");
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
  on_key_down.AddListener([this](poly::ui::KeyEvent& e) {
    if (e.key_code() == 115) {
      emulator()->graphics_system()->RequestFrameTrace();
      e.set_handled(true);
      return;
    }
  });
  return true;
}

void MainWindow::OnClose() {
  loop_.Quit();

  // TODO(benvanik): proper exit.
  PLOGI("User-initiated death!");
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
