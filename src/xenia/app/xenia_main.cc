/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"

#include "xenia/app/emulator_window.h"

#include <QApplication>

namespace xe {
namespace app {

int xenia_main(const std::vector<std::wstring>& args) {
  Profiler::Initialize();
  Profiler::ThreadEnter("main");

  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  int argc = 1;
  char* argv[] = {"xenia", nullptr};
  QApplication app(argc, argv);
  EmulatorWindow main_wnd;
  main_wnd.setWindowIcon(QIcon(":/icon.ico"));

  main_wnd.setFixedSize(1280, 720);
  main_wnd.show();
  if (args.size() >= 2) {
    // Launch the path passed in args[1].
    main_wnd.Launch(args[1]);
  }

  int rc = app.exec();

  Profiler::Dump();
  Profiler::Shutdown();
  return rc;
}

}  // namespace app
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia", L"xenia some.xex", xe::app::xenia_main);
