/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_UI_VIEWS_CPU_CPU_VIEW_H_
#define XENIA_DEBUG_UI_VIEWS_CPU_CPU_VIEW_H_

#include <memory>
#include <string>

#include "xenia/debug/ui/view.h"
#include "xenia/debug/ui/views/cpu/call_stack_control.h"
#include "xenia/debug/ui/views/cpu/register_list_control.h"

namespace xe {
namespace debug {
namespace ui {
namespace views {
namespace cpu {

class CpuView : public View {
 public:
  CpuView();
  ~CpuView() override;

  el::Element* BuildUI() override;

  void Setup(xe::debug::DebugClient* client) override;

 protected:
  void UpdateElementState();
  void UpdateModuleList();
  void UpdateFunctionList();
  void UpdateThreadList();

  void SwitchCurrentThread(model::Thread* thread);

  // TODO(benvanik): better state machine.
  model::Thread* current_thread_ = nullptr;

  RegisterListControl gr_registers_control_;
  RegisterListControl fr_registers_control_;
  RegisterListControl vr_registers_control_;
  RegisterListControl host_registers_control_;
  CallStackControl call_stack_control_;
};

}  // namespace cpu
}  // namespace views
}  // namespace ui
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_UI_VIEWS_CPU_CPU_VIEW_H_
