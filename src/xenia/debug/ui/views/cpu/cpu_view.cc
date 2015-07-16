/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "el/animation_manager.h"
#include "xenia/debug/ui/views/cpu/cpu_view.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

CpuView::CpuView() : View("CPU") {}

CpuView::~CpuView() = default;

el::Element* CpuView::BuildUI() {
  using namespace el::dsl;
  el::AnimationBlocker animation_blocker;

  auto functions_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(
              LayoutBoxNode()
                  .gravity(Gravity::kTop | Gravity::kLeftRight)
                  .distribution(LayoutDistribution::kAvailable)
                  .axis(Axis::kX)
                  .skin("button_group")
                  .child(ButtonNode("?"))
                  .child(
                      DropDownButtonNode().item("Module").item("Module").item(
                          "Module")))
          .child(ListBoxNode()
                     .gravity(Gravity::kAll)
                     .item("fn")
                     .item("fn")
                     .item("fn")
                     .item("fn"))
          .child(LayoutBoxNode()
                     .gravity(Gravity::kBottom | Gravity::kLeftRight)
                     .distribution(LayoutDistribution::kAvailable)
                     .axis(Axis::kX)
                     .child(TextBoxNode()
                                .type(EditType::kSearch)
                                .placeholder("Filter")));

  auto source_code_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(
              LayoutBoxNode()
                  .gravity(Gravity::kTop | Gravity::kLeftRight)
                  .distribution(LayoutDistribution::kGravity)
                  .distribution_position(LayoutDistributionPosition::kLeftTop)
                  .axis(Axis::kX)
                  .child(ButtonNode("A")))
          .child(TextBoxNode("source!")
                     .gravity(Gravity::kAll)
                     .is_multiline(true)
                     .is_read_only(true));

  auto register_list_node = ListBoxNode()
                                .gravity(Gravity::kAll)
                                .item("A")
                                .item("A")
                                .item("A")
                                .item("A");
  auto source_registers_node =
      TabContainerNode()
          .gravity(Gravity::kAll)
          .tab(ButtonNode("GPR"), CloneNode(register_list_node))
          .tab(ButtonNode("FPR"), CloneNode(register_list_node))
          .tab(ButtonNode("VMX"), CloneNode(register_list_node));

  auto source_tools_node =
      TabContainerNode()
          .gravity(Gravity::kAll)
          .align(Align::kLeft)
          .tab(ButtonNode("Stack"), LabelNode("STACK"))
          .tab(ButtonNode("BPs"), LabelNode("BREAKPOINTS"));

  auto source_pane_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(
              LayoutBoxNode()
                  .id("source_toolbar")
                  .gravity(Gravity::kTop | Gravity::kLeftRight)
                  .distribution(LayoutDistribution::kGravity)
                  .distribution_position(LayoutDistributionPosition::kLeftTop)
                  .axis(Axis::kX)
                  .child(ButtonNode("button"))
                  .child(ButtonNode("button"))
                  .child(ButtonNode("button")))
          .child(LayoutBoxNode()
                     .gravity(Gravity::kAll)
                     .distribution(LayoutDistribution::kAvailable)
                     .child(SplitContainerNode()
                                .gravity(Gravity::kAll)
                                .axis(Axis::kX)
                                .fixed_pane(FixedPane::kSecond)
                                .value(128)
                                .pane(SplitContainerNode()
                                          .gravity(Gravity::kAll)
                                          .axis(Axis::kY)
                                          .fixed_pane(FixedPane::kSecond)
                                          .value(180)
                                          .pane(source_code_node)
                                          .pane(source_registers_node))
                                .pane(source_tools_node)));

  auto memory_pane_node = LabelNode("MEMORY");

  auto node = SplitContainerNode()
                  .gravity(Gravity::kAll)
                  .axis(Axis::kY)
                  .min(128)
                  .max(512)
                  .value(128)
                  .pane(functions_node)
                  .pane(SplitContainerNode()
                            .gravity(Gravity::kAll)
                            .axis(Axis::kY)
                            .value(800)
                            .pane(source_pane_node)
                            .pane(memory_pane_node));

  root_element_.set_gravity(Gravity::kAll);
  root_element_.set_layout_distribution(LayoutDistribution::kAvailable);
  root_element_.LoadNodeTree(node);
  root_element_.GetElementsById({
      //
  });
  return &root_element_;
}

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
