/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_VIEWS_GPU_GPU_VIEW_H_
#define XENIA_DEBUG_UI_VIEWS_GPU_GPU_VIEW_H_

#include <memory>
#include <string>

#include "xenia/debug/ui/view.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace gpu {

class GpuView : public View {
 public:
  GpuView();
  ~GpuView() override;

  el::Element* BuildUI() override;

  void Setup(xe::debug::DebugClient* client) override;

 protected:
};

}  // namespace gpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_VIEWS_GPU_GPU_VIEW_H_
