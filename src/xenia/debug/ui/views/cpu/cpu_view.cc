/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "el/animation_manager.h"
#include "xenia/base/string_buffer.h"
#include "xenia/debug/ui/views/cpu/cpu_view.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

CpuView::CpuView() : View("CPU") {}

CpuView::~CpuView() = default;

el::Element* CpuView::BuildUI() {
  using namespace el::dsl;  // NOLINT(build/namespaces)
  el::AnimationBlocker animation_blocker;

  auto functions_node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .axis(Axis::kY)
          .child(LayoutBoxNode()
                     .gravity(Gravity::kTop | Gravity::kLeftRight)
                     .distribution(LayoutDistribution::kAvailable)
                     .axis(Axis::kX)
                     .skin("button_group")
                     .child(DropDownButtonNode().id("module_dropdown")))
          .child(ListBoxNode().id("function_listbox").gravity(Gravity::kAll))
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
                     .id("source_textbox")
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
                  .child(DropDownButtonNode().id("thread_dropdown")))
          .child(LayoutBoxNode()
                     .id("source_content")
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
  handler_ = std::make_unique<el::EventHandler>(&root_element_);

  handler_->Listen(el::EventType::kChanged, TBIDC("module_dropdown"),
                   [this](const el::Event& ev) {
                     UpdateFunctionList();
                     return true;
                   });
  handler_->Listen(
      el::EventType::kChanged, TBIDC("thread_dropdown"),
      [this](const el::Event& ev) {
        auto thread_dropdown = root_element_.GetElementById<el::DropDownButton>(
            TBIDC("thread_dropdown"));
        auto thread_handle = uint32_t(thread_dropdown->selected_item_id());
        if (thread_handle) {
          current_thread_ = system()->GetThreadByHandle(thread_handle);
        } else {
          current_thread_ = nullptr;
        }
        UpdateThreadCallStack(current_thread_);
        return true;
      });

  return &root_element_;
}

void CpuView::Setup(DebugClient* client) {
  client_ = client;

  system()->on_execution_state_changed.AddListener(
      [this]() { UpdateElementState(); });
  system()->on_modules_updated.AddListener([this]() { UpdateModuleList(); });
  system()->on_threads_updated.AddListener([this]() { UpdateThreadList(); });
  system()->on_thread_call_stack_updated.AddListener(
      [this](model::Thread* thread) {
        if (thread == current_thread_) {
          UpdateThreadCallStack(thread);
        }
      });
}

void CpuView::UpdateElementState() {
  root_element_.GetElementsById({
      //
  });
}

void CpuView::UpdateModuleList() {
  el::DropDownButton* module_dropdown;
  root_element_.GetElementsById({
      {TBIDC("module_dropdown"), &module_dropdown},
  });
  auto module_items = module_dropdown->default_source();
  auto modules = system()->modules();
  bool is_first = module_items->size() == 0;
  for (size_t i = module_items->size(); i < modules.size(); ++i) {
    auto module = modules[i];
    auto item = std::make_unique<el::GenericStringItem>(module->name());
    item->id = module->module_handle();
    module_items->push_back(std::move(item));
  }
  if (is_first) {
    module_dropdown->set_value(static_cast<int>(module_items->size() - 1));
  }
}

void CpuView::UpdateFunctionList() {
  el::DropDownButton* module_dropdown;
  el::ListBox* function_listbox;
  root_element_.GetElementsById({
      {TBIDC("module_dropdown"), &module_dropdown},
      {TBIDC("function_listbox"), &function_listbox},
  });
  auto module_handle = module_dropdown->selected_item_id();
  auto module = system()->GetModuleByHandle(module_handle);
  if (!module) {
    function_listbox->default_source()->clear();
    return;
  }
  // TODO(benvanik): fetch list.
}

void CpuView::UpdateThreadList() {
  el::DropDownButton* thread_dropdown;
  root_element_.GetElementsById({
      {TBIDC("thread_dropdown"), &thread_dropdown},
  });
  auto thread_items = thread_dropdown->default_source();
  auto threads = system()->threads();
  bool is_first = thread_items->size() == 0;
  for (size_t i = 0; i < thread_items->size(); ++i) {
    auto thread = threads[i];
    auto item = thread_items->at(i);
    item->str = thread->to_string();
  }
  for (size_t i = thread_items->size(); i < threads.size(); ++i) {
    auto thread = threads[i];
    auto item = std::make_unique<el::GenericStringItem>(thread->to_string());
    item->id = thread->thread_handle();
    thread_items->push_back(std::move(item));
  }
  if (is_first) {
    thread_dropdown->set_value(static_cast<int>(thread_items->size() - 1));
  }
}

void CpuView::UpdateThreadCallStack(model::Thread* thread) {
  auto textbox =
      root_element_.GetElementById<el::TextBox>(TBIDC("source_textbox"));
  if (!thread) {
    textbox->set_text("no thread");
    return;
  }
  auto& call_stack = thread->call_stack();
  StringBuffer str;
  for (size_t i = 0; i < call_stack.size(); ++i) {
    auto& frame = call_stack[i];
    size_t ordinal = call_stack.size() - i - 1;
    if (frame.guest_pc) {
      str.AppendFormat("  %.2lld %.16llX %.8X %s", ordinal, frame.host_pc,
                       frame.guest_pc, frame.name);
    } else {
      str.AppendFormat("  %.2lld %.16llX          %s", ordinal, frame.host_pc,
                       frame.name);
    }
    str.Append('\n');
  }
  textbox->set_text(str.to_string());
}

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
