/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOW_DEMO_H_
#define XENIA_UI_WINDOW_DEMO_H_

#include <memory>
#include <string>

#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app.h"

namespace xe {
namespace ui {

class WindowDemoApp : public WindowedApp {
 public:
  ~WindowDemoApp();

  bool OnInitialize() override;

 protected:
  explicit WindowDemoApp(WindowedAppContext& app_context,
                         const std::string_view name)
      : WindowedApp(app_context, name) {}

  virtual std::unique_ptr<GraphicsProvider> CreateGraphicsProvider() const = 0;

 private:
  std::unique_ptr<GraphicsProvider> graphics_provider_;
  std::unique_ptr<Window> window_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOW_DEMO_H_
