/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_APPLICATION_H_
#define XENIA_DEBUG_UI_APPLICATION_H_

#include <memory>

#include "xenia/debug/client/xdp/xdp_client.h"
#include "xenia/debug/ui/model/system.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace debug {
namespace ui {

class MainWindow;

class Application {
 public:
  ~Application();

  static std::unique_ptr<Application> Create();
  static Application* current();

  xe::ui::Loop* loop() { return loop_.get(); }
  MainWindow* main_window() const { return main_window_.get(); }
  client::xdp::XdpClient* client() { return &client_; }
  model::System* system() const { return system_.get(); }

  void Quit();

 private:
  Application();

  bool Initialize();

  std::unique_ptr<xe::ui::Loop> loop_;
  std::unique_ptr<MainWindow> main_window_;
  client::xdp::XdpClient client_;

  std::unique_ptr<model::System> system_;
};

}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_APPLICATION_H_
