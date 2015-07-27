/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "el/animation_manager.h"
#include "xenia/debug/ui/views/gpu/gpu_view.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace gpu {

GpuView::GpuView() : View("GPU") {}

GpuView::~GpuView() = default;

el::Element* GpuView::BuildUI() {
  using namespace el::dsl;
  el::AnimationBlocker animation_blocker;

  auto node = LabelNode("TODO");

  root_element_.set_gravity(Gravity::kAll);
  root_element_.set_layout_distribution(LayoutDistribution::kAvailable);
  root_element_.LoadNodeTree(node);
  root_element_.GetElementsById({
      //
  });
  handler_ = std::make_unique<el::EventHandler>(&root_element_);
  return &root_element_;
}

void GpuView::Setup(xe::debug::client::xdp::XdpClient* client) {
  //
}

}  // namespace gpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
