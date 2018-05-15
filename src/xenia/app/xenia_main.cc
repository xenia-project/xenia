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
#include "xenia/vfs/devices/host_path_device.h"

#include "xenia/app/emulator_window.h"

#include <QApplication>

DEFINE_bool(mount_scratch, false, "Enable scratch mount");
DEFINE_bool(mount_cache, false, "Enable cache mount");

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
  
  main_wnd.setFixedSize(1280, 720);
  
  /*
  if (FLAGS_mount_scratch) {
    auto scratch_device = std::make_unique<xe::vfs::HostPathDevice>(
        "\\SCRATCH", L"scratch", false);
    if (!scratch_device->Initialize()) {
      XELOGE("Unable to scan scratch path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(scratch_device))) {
        XELOGE("Unable to register scratch path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("scratch:", "\\SCRATCH");
      }
    }
  }

  if (FLAGS_mount_cache) {
    auto cache0_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE0", L"cache0", false);
    if (!cache0_device->Initialize()) {
      XELOGE("Unable to scan cache0 path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(cache0_device))) {
        XELOGE("Unable to register cache0 path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("cache0:", "\\CACHE0");
      }
    }

    auto cache1_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE1", L"cache1", false);
    if (!cache1_device->Initialize()) {
      XELOGE("Unable to scan cache1 path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(cache1_device))) {
        XELOGE("Unable to register cache1 path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("cache1:", "\\CACHE1");
      }
    }
  }

  // Set a debug handler.
  // This will respond to debugging requests so we can open the debug UI.
  std::unique_ptr<xe::debug::ui::DebugWindow> debug_window;
  if (FLAGS_debug) {
    emulator->processor()->set_debug_listener_request_handler(
        [&](xe::cpu::Processor* processor) {
          if (debug_window) {
            return debug_window.get();
          }
          emulator_window->loop()->PostSynchronous([&]() {
            debug_window = xe::debug::ui::DebugWindow::Create(
                emulator.get(), emulator_window->loop());
            debug_window->window()->on_closed.AddListener(
                [&](xe::ui::UIEvent* e) {
                  emulator->processor()->set_debug_listener(nullptr);
                  emulator_window->loop()->Post(
                      [&]() { debug_window.reset(); });
                });
          });
          return debug_window.get();
        });
  }
  */

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
