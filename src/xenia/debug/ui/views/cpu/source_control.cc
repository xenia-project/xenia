/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/views/cpu/source_control.h"

#include "el/animation_manager.h"
#include "xenia/base/string_buffer.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

SourceControl::SourceControl() = default;

SourceControl::~SourceControl() = default;

el::Element* SourceControl::BuildUI() {
  using namespace el::dsl;  // NOLINT(build/namespaces)
  el::AnimationBlocker animation_blocker;

  auto node = LayoutBoxNode()
                  .gravity(Gravity::kAll)
                  .distribution(LayoutDistribution::kAvailable)
                  .axis(Axis::kY)
                  .child(LayoutBoxNode()
                             .gravity(Gravity::kTop | Gravity::kLeftRight)
                             .distribution(LayoutDistribution::kGravity)
                             .distribution_position(
                                 LayoutDistributionPosition::kLeftTop)
                             .axis(Axis::kX)
                             .child(ButtonNode("A")))
                  .child(TextBoxNode("source!")
                             .id("source_textbox")
                             .gravity(Gravity::kAll)
                             .is_multiline(true)
                             .is_read_only(true));

  root_element_.set_gravity(Gravity::kAll);
  root_element_.set_layout_distribution(LayoutDistribution::kAvailable);
  root_element_.LoadNodeTree(node);

  handler_ = std::make_unique<el::EventHandler>(&root_element_);

  return &root_element_;
}

void SourceControl::Setup(DebugClient* client) {
  client_ = client;

  // system()->on_thread_state_updated.AddListener([this](model::Thread* thread)
  // {
  //  if (thread == thread_) {
  //    InvalidateCallStack();
  //  }
  //});
}

void SourceControl::set_thread(model::Thread* thread) {
  thread_ = thread;
  // InvalidateCallStack();
}

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
