/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cinttypes>
#include <cstdlib>

#include "el/animation_manager.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/string_util.h"
#include "xenia/cpu/x64_context.h"
#include "xenia/debug/ui/views/cpu/register_list_control.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

using xe::cpu::frontend::PPCRegister;
using xe::cpu::X64Register;

enum class RegisterType {
  kControl,
  kInteger,
  kFloat,
  kVector,
};

class RegisterItem : public el::GenericStringItem {
 public:
  const char* name() { return name_.c_str(); }

  virtual void set_thread(model::Thread* thread) { thread_ = thread; }

 protected:
  RegisterItem(RegisterSet set, RegisterType type, int ordinal)
      : GenericStringItem(""), set_(set), type_(type), ordinal_(ordinal) {
    tag.set_integer(ordinal);
  }

  RegisterSet set_;
  RegisterType type_;
  int ordinal_;
  model::Thread* thread_ = nullptr;
  std::string name_;
};

class GuestRegisterItem : public RegisterItem {
 public:
  GuestRegisterItem(RegisterSet set, RegisterType type, PPCRegister reg)
      : RegisterItem(set, type, static_cast<int>(reg)), reg_(reg) {
    name_ = xe::cpu::frontend::PPCContext::GetRegisterName(reg_);
  }

  void set_thread(model::Thread* thread) override {
    RegisterItem::set_thread(thread);
    if (thread_) {
      str = thread_->guest_context()->GetStringFromValue(reg_);
    } else {
      str = "unknown";
    }
  }

 private:
  PPCRegister reg_;
};

class HostRegisterItem : public RegisterItem {
 public:
  HostRegisterItem(RegisterSet set, RegisterType type, X64Register reg)
      : RegisterItem(set, type, static_cast<int>(reg)), reg_(reg) {
    name_ = xe::cpu::X64Context::GetRegisterName(reg_);
  }

  void set_thread(model::Thread* thread) override {
    RegisterItem::set_thread(thread);
    if (thread_) {
      str = thread_->host_context()->GetStringFromValue(reg_);
    } else {
      str = "unknown";
    }
  }

 private:
  X64Register reg_;
};

class RegisterItemElement : public el::LayoutBox {
 public:
  RegisterItemElement(RegisterItem* item, RegisterItemSource* source,
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

    auto node = LayoutBoxNode()
                    .axis(Axis::kX)
                    .gravity(Gravity::kLeft)
                    .distribution(LayoutDistribution::kAvailable)
                    .child(LabelNode(item_->name())
                               .ignore_input(true)
                               .width(45_px)
                               .gravity(Gravity::kLeft))
                    .child(TextBoxNode(item_->str.c_str())
                               .gravity(Gravity::kLeftRight));
    content_root()->LoadNodeTree(node);
  }

  bool OnEvent(const el::Event& ev) override {
    return el::LayoutBox::OnEvent(ev);
  }

 private:
  RegisterItem* item_ = nullptr;
  RegisterItemSource* source_ = nullptr;
  el::ListItemObserver* source_viewer_ = nullptr;
  size_t index_ = 0;
};

class RegisterItemSource : public el::ListItemSourceList<RegisterItem> {
 public:
  el::Element* CreateItemElement(size_t index,
                                 el::ListItemObserver* viewer) override {
    return new RegisterItemElement(at(index), this, viewer, index);
  }
};

void DefineRegisterItem(RegisterItemSource* item_source, RegisterSet set,
                        RegisterType type, PPCRegister reg) {
  auto item = std::make_unique<GuestRegisterItem>(set, type, reg);
  item->tag.set_integer(static_cast<int>(reg));
  item_source->push_back(std::move(item));
}
void DefineRegisterItem(RegisterItemSource* item_source, RegisterSet set,
                        RegisterType type, X64Register reg) {
  auto item = std::make_unique<HostRegisterItem>(set, type, reg);
  item->tag.set_integer(static_cast<int>(reg));
  item_source->push_back(std::move(item));
}

RegisterListControl::RegisterListControl(RegisterSet set)
    : set_(set), item_source_(new RegisterItemSource()) {
  switch (set_) {
    case RegisterSet::kGeneral: {
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         PPCRegister::kLR);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         PPCRegister::kCTR);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kControl,
                         PPCRegister::kXER);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kControl,
                         PPCRegister::kCR);
      for (size_t i = 0; i < 32; ++i) {
        DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                           static_cast<PPCRegister>(
                               static_cast<size_t>(PPCRegister::kR0) + i));
      }
    } break;
    case RegisterSet::kFloat: {
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kControl,
                         PPCRegister::kFPSCR);
      for (size_t i = 0; i < 32; ++i) {
        DefineRegisterItem(item_source_.get(), set_, RegisterType::kFloat,
                           static_cast<PPCRegister>(
                               static_cast<size_t>(PPCRegister::kFR0) + i));
      }
    } break;
    case RegisterSet::kVector: {
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kControl,
                         PPCRegister::kVSCR);
      for (size_t i = 0; i < 128; ++i) {
        DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                           static_cast<PPCRegister>(
                               static_cast<size_t>(PPCRegister::kVR0) + i));
      }
    } break;
    case RegisterSet::kHost: {
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRip);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kControl,
                         X64Register::kEflags);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRax);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRcx);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRdx);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRbx);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRsp);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRbp);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRsi);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kRdi);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR8);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR9);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR10);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR11);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR12);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR13);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR14);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kInteger,
                         X64Register::kR15);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm0);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm1);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm2);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm3);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm4);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm5);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm6);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm7);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm8);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm9);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm10);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm11);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm12);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm13);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm14);
      DefineRegisterItem(item_source_.get(), set_, RegisterType::kVector,
                         X64Register::kXmm15);
    } break;
  }
}

RegisterListControl::~RegisterListControl() = default;

el::Element* RegisterListControl::BuildUI() {
  using namespace el::dsl;  // NOLINT(build/namespaces)
  el::AnimationBlocker animation_blocker;

  auto node =
      LayoutBoxNode()
          .gravity(Gravity::kAll)
          .distribution(LayoutDistribution::kAvailable)
          .child(ListBoxNode().id("register_listbox").gravity(Gravity::kAll));

  root_element_.set_gravity(Gravity::kAll);
  root_element_.set_layout_distribution(LayoutDistribution::kAvailable);
  root_element_.LoadNodeTree(node);

  auto register_listbox =
      root_element_.GetElementById<el::ListBox>(TBIDC("register_listbox"));
  register_listbox->set_source(item_source_.get());
  register_listbox->scroll_container()->set_scroll_mode(
      el::ScrollMode::kAutoXAutoY);

  handler_ = std::make_unique<el::EventHandler>(&root_element_);

  return &root_element_;
}

void RegisterListControl::Setup(DebugClient* client) {
  client_ = client;

  system()->on_thread_state_updated.AddListener([this](model::Thread* thread) {
    if (thread == thread_) {
      InvalidateRegisters();
    }
  });
}

void RegisterListControl::set_thread(model::Thread* thread) {
  thread_ = thread;
  InvalidateRegisters();
}

void RegisterListControl::InvalidateRegisters() {
  for (size_t i = 0; i < item_source_->size(); ++i) {
    item_source_->at(i)->set_thread(thread_);
  }

  auto register_listbox =
      root_element_.GetElementById<el::ListBox>(TBIDC("register_listbox"));
  register_listbox->InvalidateList();
}

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe
