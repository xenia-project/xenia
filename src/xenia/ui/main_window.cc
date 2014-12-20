/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/ui/main_window.h>

#include <poly/logging.h>

namespace xe {
namespace ui {

MainWindow::MainWindow() : PlatformWindow(L"xenia") {}

MainWindow::~MainWindow() {}

void MainWindow::Start() {
  loop_.Post([this]() {
    if (!Initialize()) {
      PFATAL("Failed to initialize main window");
      exit(1);
    }
  });
}

bool MainWindow::Initialize() {
  if (!Window::Initialize()) {
    return false;
  }
  //
  return true;
}

}  // namespace ui
}  // namespace xe
