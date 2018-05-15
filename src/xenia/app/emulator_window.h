/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_MAIN_WINDOW_H_
#define XENIA_APP_MAIN_WINDOW_H_

#include <QMainWindow>
#include <QVulkanInstance>
#include <QWindow>

#include "xenia/emulator.h"
#include "xenia/ui/graphics_context.h"
#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace app {

class VulkanWindow;
class VulkanRenderer;

class EmulatorWindow : public QMainWindow {
  Q_OBJECT

 public:
  EmulatorWindow();

  bool Launch(const std::wstring& path);

  xe::Emulator* emulator() { return emulator_.get(); }

 protected:
  // Events

 private slots:

 private:
  void CreateMenuBar();

  bool InitializeVulkan();

  std::unique_ptr<xe::Emulator> emulator_;

  std::unique_ptr<QWindow> graphics_window_;
  std::unique_ptr<ui::GraphicsProvider> graphics_provider_;

  std::unique_ptr<QVulkanInstance> vulkan_instance_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_UI_QT_MAIN_WINDOW_H_