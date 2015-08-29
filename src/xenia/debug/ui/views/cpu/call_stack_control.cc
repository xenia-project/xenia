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
#include "xenia/debug/ui/views/cpu/call_stack_control.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

class CallStackItem : public el::GenericStringItem {
 public:
  CallStackItem(size_t ordinal, const ThreadCallStackFrame* frame)
      : GenericStringItem(""), ordinal_(ordinal), frame_(frame) {
    StringBuffer sb;
    if (frame_->guest_pc) {
      sb.AppendFormat("  %.2lld %.16llX %.8X %s", ordinal_, frame_->host_pc,
                      frame_->guest_pc, frame_->name);
    } else {
      sb.AppendFormat("  %.2lld %.16llX          %s", ordinal_, frame_->host_pc,
                      frame_->name);
    }
    str = sb.to_string();
  }

 private:
  size_t ordinal_ = 0;
  const ThreadCallStackFrame* frame_ = nullptr;
};

class CallStackItemElement : public el::LayoutBox {
 public:
  CallStackItemElement(CallStackItem* item, CallStackItemSource* source,
                       el::ListItemObserver* source_viewer, size_t index)
      : item_(item),
        source_(source),
        source_viewer_(source_viewer),
        index_(index) {
    set_background_skin(TBIDC("ListItem"));
    set_axis(el::Axis::kY);
    set_gravity(el::Gravity::kAll);
    set_layout_position(el::LayoutPosition::kLeftTop);
    set_layout_distribution(el::LayoutDistribution::kAvailable);
    set_layout_distribution_position(el::LayoutDistributionPosition::kLeftTop);
    set_layout_size(el::LayoutSize::kAvailable);
    set_paint_overflow_fadeout(false);

    using namespace el::dsl;  // NOLINT(build/namespaces)
    el::AnimationBlocker animation_blocker;

    auto node = LabelNode(item_->str.c_str())
                    .gravity(Gravity::kLeft)
                    .ignore_input(true)
                    .text_align(el::TextAlign::kLeft);
    content_root()->LoadNodeTree(node);
  }

  bool OnEvent(const el::Event& ev) override {
    return el::LayoutBox::OnEvent(ev);
  }

 private:
  CallStackItem* item_ = nullptr;
  CallStackItemSource* source_ = nullptr;
  el::ListItemObserver* source_viewer_ = nullptr;
  size_t index_ = 0;
};

class CallStackItemSource : public el::ListItemSourceList<CallStackItem> {
 public:
  el::Element* CreateItemElement(size_t index,
                                 el::ListItemObserver* viewer) override {
    return new CallStackItemElement(at(index), this, viewer, index);
  }
};

CallStackControl::CallStackControl()
    : item_source_(new CallStackItemSource()) {}

CallStackControl::~CallStackControl() = default;

el::Element* CallStackControl::BuildUI() {
  using namespace el::dsl;  // NOLINT(build/namespaces)
  el::AnimationBlocker animation_blocker;

  auto node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .child(ListBoxNode().id("stack_listbox").gravity(Gravity::kAll));

  root_element_.set_gravity(Gravity::kAll);
  root_element_.set_layout_distribution(LayoutDistribution::kAvailable);
  root_element_.LoadNodeTree(node);

  auto stack_listbox =
      root_element_.GetElementById<el::ListBox>(TBIDC("stack_listbox"));
  stack_listbox->set_source(item_source_.get());
  stack_listbox->scroll_container()->set_scroll_mode(
      el::ScrollMode::kAutoXAutoY);

  handler_ = std::make_unique<el::EventHandler>(&root_element_);

  return &root_element_;
}

void CallStackControl::Setup(DebugClient* client) {
  client_ = client;

  system()->on_thread_state_updated.AddListener([this](model::Thread* thread) {
    if (thread == thread_) {
      InvalidateCallStack();
    }
  });
}

void CallStackControl::set_thread(model::Thread* thread) {
  thread_ = thread;
  InvalidateCallStack();
}

void CallStackControl::InvalidateCallStack() {
  item_source_->clear();
  if (!thread_) {
    return;
  }
  auto& call_stack = thread_->call_stack();
  for (size_t i = 0; i < call_stack.size(); ++i) {
    auto& frame = call_stack[i];
    size_t ordinal = call_stack.size() - i - 1;
    auto item = std::make_unique<CallStackItem>(ordinal, &frame);
    item->tag.set_integer(static_cast<int>(i));
    item_source_->push_back(std::move(item));
  }
  item_source_->InvokeAllItemsRemoved();
}

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
