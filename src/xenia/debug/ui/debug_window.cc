/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/ui/debug_window.h"

#include <algorithm>
#include <cinttypes>
#include <utility>

#include "third_party/capstone/include/capstone/capstone.h"
#include "third_party/capstone/include/capstone/x86.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"
#include "xenia/base/clock.h"
#include "xenia/base/fuzzy.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/string_util.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/windowed_app_context.h"

DEFINE_bool(imgui_debug, false, "Show ImGui debugging tools.", "UI");

namespace xe {
namespace debug {
namespace ui {

using xe::cpu::Breakpoint;
using xe::kernel::XModule;
using xe::kernel::XObject;
using xe::kernel::XThread;
using xe::ui::KeyEvent;
using xe::ui::MenuItem;
using xe::ui::MouseEvent;
using xe::ui::UIEvent;

void DebugWindow::DebugDialog::OnDraw(ImGuiIO& io) {
  debug_window_.DrawFrame(io);
}

static const std::string kBaseTitle = "Xenia Debugger";

DebugWindow::DebugWindow(Emulator* emulator,
                         xe::ui::WindowedAppContext& app_context)
    : emulator_(emulator),
      processor_(emulator->processor()),
      app_context_(app_context),
      window_(xe::ui::Window::Create(app_context_, kBaseTitle, 1500, 1000)) {
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle_) != CS_ERR_OK) {
    assert_always("Failed to initialize capstone");
  }
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_OFF);
}

DebugWindow::~DebugWindow() {
  // Make sure pending functions referencing the DebugWindow are executed.
  app_context_.ExecutePendingFunctionsFromUIThread();

  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }
}

std::unique_ptr<DebugWindow> DebugWindow::Create(
    Emulator* emulator, xe::ui::WindowedAppContext& app_context) {
  std::unique_ptr<DebugWindow> debug_window(
      new DebugWindow(emulator, app_context));
  if (!debug_window->Initialize()) {
    xe::FatalError("Failed to initialize debug window");
    return nullptr;
  }

  return debug_window;
}

bool DebugWindow::Initialize() {
  // Main menu.
  auto main_menu = MenuItem::Create(MenuItem::Type::kNormal);
  auto file_menu = MenuItem::Create(MenuItem::Type::kPopup, "&File");
  {
    file_menu->AddChild(
        MenuItem::Create(MenuItem::Type::kString, "&Close", "Alt+F4",
                         [this]() { window_->RequestClose(); }));
  }
  main_menu->AddChild(std::move(file_menu));
  window_->SetMainMenu(std::move(main_menu));

  // Open the window once it's configured.
  if (!window_->Open()) {
    XELOGE("Failed to open the platform window for the debugger");
    return false;
  }

  // Setup drawing to the window.

  xe::ui::GraphicsProvider& graphics_provider =
      *emulator_->graphics_system()->provider();

  presenter_ = graphics_provider.CreatePresenter();
  if (!presenter_) {
    XELOGE("Failed to initialize the presenter for the debugger");
    return false;
  }

  immediate_drawer_ = graphics_provider.CreateImmediateDrawer();
  if (!immediate_drawer_) {
    XELOGE("Failed to initialize the immediate drawer for the debugger");
    return false;
  }
  immediate_drawer_->SetPresenter(presenter_.get());

  imgui_drawer_ = std::make_unique<xe::ui::ImGuiDrawer>(window_.get(), 0);
  imgui_drawer_->SetPresenterAndImmediateDrawer(presenter_.get(),
                                                immediate_drawer_.get());
  debug_dialog_ =
      std::unique_ptr<DebugDialog>(new DebugDialog(imgui_drawer_.get(), *this));

  // Update the cache before the first frame.
  UpdateCache();

  // Begin drawing.
  window_->SetPresenter(presenter_.get());

  return true;
}

void DebugWindow::DrawFrame(ImGuiIO& io) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(-1, 0));
  ImGui::Begin("main_window", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::SetWindowPos(ImVec2(0, 0));
  ImGui::SetWindowSize(io.DisplaySize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

  constexpr float kSplitterWidth = 5.0f;
  static float function_pane_width = 150.0f;
  static float source_pane_width = 600.0f;
  static float registers_pane_width = 150.0f;
  static float bottom_panes_height = 300.0f;
  static float breakpoints_pane_width = 300.0f;
  float top_panes_height =
      ImGui::GetContentRegionAvail().y - bottom_panes_height;
  float log_pane_width =
      ImGui::GetContentRegionAvail().x - breakpoints_pane_width;

  ImGui::BeginChild("##toolbar", ImVec2(0, 25), true);
  DrawToolbar();
  ImGui::EndChild();

  if (!cache_.is_running) {
    // Disabled state?
    // https://github.com/ocornut/imgui/issues/211
  }

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginChild("##function_pane",
                    ImVec2(function_pane_width, top_panes_height), true);
  DrawFunctionsPane();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::InvisibleButton("##vsplitter0",
                         ImVec2(kSplitterWidth, top_panes_height));
  if (ImGui::IsItemActive()) {
    function_pane_width += io.MouseDelta.x;
    function_pane_width = xe::clamp_float(function_pane_width, 30.0f, FLT_MAX);
  }
  ImGui::SameLine();
  ImGui::BeginChild("##source_pane",
                    ImVec2(source_pane_width, top_panes_height), true);
  DrawSourcePane();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::InvisibleButton("##vsplitter1",
                         ImVec2(kSplitterWidth, top_panes_height));
  if (ImGui::IsItemActive()) {
    source_pane_width += io.MouseDelta.x;
    source_pane_width = xe::clamp_float(source_pane_width, 30.0f, FLT_MAX);
  }
  ImGui::SameLine();
  ImGui::BeginChild("##registers_pane",
                    ImVec2(registers_pane_width, top_panes_height), true);
  DrawRegistersPane();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::InvisibleButton("##vsplitter2",
                         ImVec2(kSplitterWidth, top_panes_height));
  if (ImGui::IsItemActive()) {
    registers_pane_width += io.MouseDelta.x;
    registers_pane_width =
        xe::clamp_float(registers_pane_width, 30.0f, FLT_MAX);
  }
  ImGui::SameLine();
  ImGui::BeginChild("##right_pane", ImVec2(0, top_panes_height), true);
  ImGui::BeginGroup();
  ImGui::RadioButton("Threads", &state_.right_pane_tab,
                     ImState::kRightPaneThreads);
  ImGui::SameLine();
  ImGui::RadioButton("Memory", &state_.right_pane_tab,
                     ImState::kRightPaneMemory);
  ImGui::EndGroup();
  ImGui::Separator();
  switch (state_.right_pane_tab) {
    case ImState::kRightPaneThreads:
      ImGui::BeginChild("##threads_pane");
      DrawThreadsPane();
      ImGui::EndChild();
      break;
    case ImState::kRightPaneMemory:
      ImGui::BeginChild("##memory_pane");
      DrawMemoryPane();
      ImGui::EndChild();
      break;
  }
  ImGui::EndChild();
  ImGui::InvisibleButton("##hsplitter0", ImVec2(-1, kSplitterWidth));
  if (ImGui::IsItemActive()) {
    bottom_panes_height -= io.MouseDelta.y;
    bottom_panes_height = xe::clamp_float(bottom_panes_height, 30.0f, FLT_MAX);
  }
  ImGui::BeginChild("##log_pane", ImVec2(log_pane_width, bottom_panes_height),
                    true);
  DrawLogPane();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::InvisibleButton("##vsplitter3",
                         ImVec2(kSplitterWidth, bottom_panes_height));
  if (ImGui::IsItemActive()) {
    breakpoints_pane_width -= io.MouseDelta.x;
    breakpoints_pane_width =
        xe::clamp_float(breakpoints_pane_width, 30.0f, FLT_MAX);
  }
  ImGui::SameLine();
  ImGui::BeginChild("##breakpoints_pane", ImVec2(0, 0), true);
  DrawBreakpointsPane();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  ImGui::PopStyleVar();
  ImGui::End();
  ImGui::PopStyleVar();

  if (cvars::imgui_debug) {
    ImGui::ShowMetricsWindow();
  }
}

void DebugWindow::DrawToolbar() {
  // TODO(benvanik): save/load database? other app options?

  // Program control.
  if (cache_.is_running) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(0.9f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    if (ImGui::Button("Pause", ImVec2(80, 0))) {
      processor_->Pause();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Interrupt the program and break into the debugger.");
    }
    ImGui::PopStyleColor(3);
  } else {
    if (ImGui::Button("Continue", ImVec2(80, 0))) {
      processor_->Continue();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Resume the program from the current location.");
    }
  }
  ImGui::SameLine();

  // Thread dropdown.
  // Fast selection of the thread and reports the currently active thread for
  // global operations.
  // TODO(benvanik): use a popup + custom rendering to get richer view.
  int current_thread_index = 0;
  StringBuffer thread_combo;
  int i = 0;
  for (auto thread_info : cache_.thread_debug_infos) {
    if (thread_info == state_.thread_info) {
      current_thread_index = i;
    }

    // Threads can be briefly invalid once destroyed and before a cache update.
    // This ensures we are accessing threads that are still valid.
    switch (thread_info->state) {
      case cpu::ThreadDebugInfo::State::kAlive:
      case cpu::ThreadDebugInfo::State::kExited:
      case cpu::ThreadDebugInfo::State::kWaiting:
        if (!thread_info->thread_handle || thread_info->thread == nullptr) {
          thread_combo.Append("(invalid)");
        } else {
          thread_combo.Append(thread_info->thread->thread_name());
        }
        break;
      case cpu::ThreadDebugInfo::State::kZombie:
        thread_combo.Append("(zombie)");
        break;
      default:
        thread_combo.Append("(invalid)");
    }

    thread_combo.Append('\0');
    ++i;
  }
  if (ImGui::Combo("##thread_combo", &current_thread_index,
                   thread_combo.buffer(), 10)) {
    // Thread changed.
    SelectThreadStackFrame(cache_.thread_debug_infos[current_thread_index], 0,
                           true);
  }
}

void DebugWindow::DrawFunctionsPane() {
  ImGui::Text("<functions>");
  // bar: analyze (?), goto current
  // combo with module (+ emulator itself?)
  // list with all functions known
  // filter box + search button -> search dialog
}

void DebugWindow::DrawSourcePane() {
  auto function = state_.function;
  if (!function) {
    ImGui::Text("(no function selected)");
    return;
  }
  ImGui::BeginGroup();
  // top bar:
  //   copy button
  //   address start - end
  //   name text box (editable)
  //   combo for interleaved + [ppc, hir, opt hir, x64 + byte with sizes]
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", function->module()->name().c_str());
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  char address_str[9];
  std::snprintf(address_str, xe::countof(address_str), "%.8X",
                function->address());
  ImGui::PushItemWidth(50);
  ImGui::InputText("##address", address_str, xe::countof(address_str),
                   ImGuiInputTextFlags_AutoSelectAll);
  ImGui::PopItemWidth();
  ImGui::SameLine();
  ImGui::Text(" - %.8X", function->end_address());
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  char name[256];
  std::strcpy(name, function->name().c_str());
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10);
  if (ImGui::InputText("##name", name, sizeof(name),
                       ImGuiInputTextFlags_AutoSelectAll)) {
    function->set_name(name);
  }
  ImGui::PopItemWidth();
  ImGui::EndGroup();

  ImGui::BeginGroup();
  ImGui::PushButtonRepeat(true);
  bool can_step = !cache_.is_running && state_.thread_info;
  if (ImGui::ButtonEx("Step PPC", ImVec2(0, 0),
                      can_step ? 0 : ImGuiItemFlags_Disabled)) {
    // By enabling the button when stepping we allow repeat behavior.
    if (processor_->execution_state() != cpu::ExecutionState::kStepping) {
      processor_->StepGuestInstruction(state_.thread_info->thread_id);
    }
  }
  ImGui::PopButtonRepeat();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Step one PPC instruction on the current thread (hold for many).");
  }
  ImGui::SameLine();
  if (state_.source_display_mode > 0) {
    // Only show x64 step button if we have x64 visible.
    ImGui::Dummy(ImVec2(4, 0));
    ImGui::SameLine();
    ImGui::PushButtonRepeat(true);
    if (ImGui::ButtonEx("Step x64", ImVec2(0, 0),
                        can_step ? 0 : ImGuiItemFlags_Disabled)) {
      // By enabling the button when stepping we allow repeat behavior.
      if (processor_->execution_state() != cpu::ExecutionState::kStepping) {
        processor_->StepHostInstruction(state_.thread_info->thread_id);
      }
    }
    ImGui::PopButtonRepeat();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Step one x64 instruction on the current thread (hold for many).");
    }
    ImGui::SameLine();
  }
  ImGui::Dummy(ImVec2(16, 0));
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    // TODO(benvanik): move clipboard abstraction into ui?
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  if (function->is_guest()) {
    const char* kSourceDisplayModes[] = {
        "PPC",
        "PPC+HIR+x64",
        "PPC+HIR (opt)+x64",
        "PPC+x64",
    };
    ImGui::PushItemWidth(90);
    ImGui::Combo("##display_mode", &state_.source_display_mode,
                 kSourceDisplayModes,
                 static_cast<int>(xe::countof(kSourceDisplayModes)));
    ImGui::PopItemWidth();
    ImGui::SameLine();
  }
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  ImGui::Text("(profile options?)");
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  ImGui::Text("(hit count)");
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(4, 0));
  ImGui::SameLine();
  ImGui::Text("(code size?)");
  ImGui::EndGroup();

  ImGui::Separator();
  ImGui::BeginChild("##code");
  if (function->is_guest()) {
    DrawGuestFunctionSource();
  } else {
    // TODO(benvanik): load PDB and display source code?
    ImGui::Text("(system function?)");
  }
  ImGui::EndChild();
}

void DebugWindow::DrawGuestFunctionSource() {
  // source code:
  //   |     8200000            ppc_label:
  //   | [I] 8200000  FFCCDDEE          ppc disam               # comment
  //   |                 v0 = add v1, v2
  //   |        A123456  xadd rax, rbx
  //     icon = active lines/etc (other threads too)
  //            color gutter background for trace data? (hit count, etc)
  //     show address + code bytes + ppc diasm with annotation comments
  //     labels get their own line with duped addresses
  //       show xrefs to labels?
  //     hir greyed and offset (background color change?)
  //     x64 greyed and offset with native address
  //     hover on registers/etc for tooltip/highlight others
  //     click register to go to location of last write
  //     click code address to jump to code
  //     click memory address to jump to memory browser
  //     if historical data for memory/etc present, show combo boxes
  auto memory = emulator_->memory();
  auto function = static_cast<cpu::GuestFunction*>(state_.function);
  auto& source_map = function->source_map();
  uint32_t source_map_index = 0;

  bool draw_hir = false;
  bool draw_hir_opt = false;
  bool draw_x64 = false;
  switch (state_.source_display_mode) {
    case 1:
      draw_hir = true;
      draw_x64 = true;
      break;
    case 2:
      draw_hir_opt = true;
      draw_x64 = true;
      break;
    case 3:
      draw_x64 = true;
      break;
  }

  auto guest_pc =
      state_.thread_info
          ? state_.thread_info->frames[state_.thread_stack_frame_index].guest_pc
          : 0;

  if (draw_hir) {
    // TODO(benvanik): get HIR and draw preamble.
  }
  if (draw_hir_opt) {
    // TODO(benvanik): get HIR and draw preamble.
  }
  if (draw_x64) {
    // x64 preamble.
    DrawMachineCodeSource(function->machine_code(), source_map[0].code_offset);
  }

  StringBuffer str;
  for (uint32_t address = function->address();
       address <= function->end_address(); address += 4) {
    ImGui::PushID(address);

    // TODO(benvanik): check other threads?
    bool is_current_instr = address == guest_pc;
    if (is_current_instr) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
      if (!draw_x64) {
        ScrollToSourceIfPcChanged();
      }
    }

    bool has_guest_bp =
        LookupBreakpointAtAddress(Breakpoint::AddressType::kGuest, address) !=
        nullptr;
    DrawBreakpointGutterButton(has_guest_bp, Breakpoint::AddressType::kGuest,
                               address);
    ImGui::SameLine();

    ImGui::Text(" %c ", is_current_instr ? '>' : ' ');
    ImGui::SameLine();

    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    cpu::ppc::DisasmPPC(address, code, &str);
    ImGui::Text("%.8X %.8X   %s", address, code, str.buffer());
    str.Reset();

    if (is_current_instr) {
      ImGui::PopStyleColor();
    }

    while (source_map_index < source_map.size() &&
           source_map[source_map_index].guest_address != address) {
      ++source_map_index;
    }
    if (source_map_index < source_map.size()) {
      if (draw_hir) {
        // TODO(benvanik): get HIR and draw for this PPC function.
      }
      if (draw_hir_opt) {
        // TODO(benvanik): get HIR and draw for this PPC function.
      }
      if (draw_x64) {
        const uint8_t* machine_code_start =
            function->machine_code() + source_map[source_map_index].code_offset;
        const size_t machine_code_length =
            (source_map_index == source_map.size() - 1
                 ? function->machine_code_length()
                 : source_map[source_map_index + 1].code_offset) -
            source_map[source_map_index].code_offset;
        DrawMachineCodeSource(machine_code_start, machine_code_length);
      }
    }

    ImGui::PopID();
  }
}

void DebugWindow::DrawMachineCodeSource(const uint8_t* machine_code_ptr,
                                        size_t length) {
  auto host_pc =
      state_.thread_info
          ? state_.thread_info->frames[state_.thread_stack_frame_index].host_pc
          : 0;

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
  auto machine_code_start_ptr = machine_code_ptr;
  size_t remaining_machine_code_size = length;
  uint64_t host_address = uint64_t(machine_code_ptr);
  cs_insn insn = {0};
  while (remaining_machine_code_size &&
         cs_disasm_iter(capstone_handle_, &machine_code_ptr,
                        &remaining_machine_code_size, &host_address, &insn)) {
    ImGui::PushID(reinterpret_cast<void*>(insn.address));

    bool is_current_instr = state_.thread_info && insn.address == host_pc;
    if (is_current_instr) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
      ScrollToSourceIfPcChanged();
    }

    bool has_host_bp = LookupBreakpointAtAddress(Breakpoint::AddressType::kHost,
                                                 insn.address) != nullptr;
    DrawBreakpointGutterButton(has_host_bp, Breakpoint::AddressType::kHost,
                               insn.address);
    ImGui::SameLine();

    ImGui::Text("    %c ", is_current_instr ? '>' : ' ');
    ImGui::SameLine();
    ImGui::Text(" %.8X        %-10s %s", uint32_t(insn.address), insn.mnemonic,
                insn.op_str);

    if (is_current_instr) {
      ImGui::PopStyleColor();
    }

    ImGui::PopID();
  }
  ImGui::PopStyleColor();
}

void DebugWindow::DrawBreakpointGutterButton(
    bool has_breakpoint, Breakpoint::AddressType address_type,
    uint64_t address) {
  ImGui::PushStyleColor(ImGuiCol_Button,
                        has_breakpoint
                            ? ImVec4(1.0f, 0.0f, 0.0f, 0.6f)
                            : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        !has_breakpoint
                            ? ImVec4(1.0f, 0.0f, 0.0f, 0.8f)
                            : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        !has_breakpoint
                            ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
                            : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
  if (ImGui::Button(" ##toggle_line_bp")) {
    if (!has_breakpoint) {
      CreateCodeBreakpoint(address_type, address);
    } else {
      auto breakpoint = LookupBreakpointAtAddress(address_type, address);
      assert_not_null(breakpoint);
      DeleteCodeBreakpoint(breakpoint);
    }
  }
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(has_breakpoint ? "Remove breakpoint at this address."
                                     : "Add a breakpoint at this address.");
  }
}

void DebugWindow::ScrollToSourceIfPcChanged() {
  if (state_.has_changed_pc) {
    // TODO(benvanik): not so annoying scroll.
    ImGui::SetScrollHereY(0.5f);
    state_.has_changed_pc = false;
  }
}

bool DebugWindow::DrawRegisterTextBox(int id, uint32_t* value) {
  char buffer[256] = {0};
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank;
  if (state_.register_input_hex) {
    input_flags |= ImGuiInputTextFlags_CharsHexadecimal |
                   ImGuiInputTextFlags_AlwaysOverwrite |
                   ImGuiInputTextFlags_NoHorizontalScroll;
    auto src_value = xe::string_util::to_hex_string(*value);
    std::strcpy(buffer, src_value.c_str());
  } else {
    input_flags |=
        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll;
    auto src_value = std::to_string(*value);
    std::strcpy(buffer, src_value.c_str());
  }
  char label[16] = {0};
  std::snprintf(label, xe::countof(label), "##iregister%d", id);
  bool any_changed = false;
  ImGui::PushItemWidth(50);
  if (ImGui::InputText(label, buffer,
                       state_.register_input_hex ? 9 : sizeof(buffer),
                       input_flags)) {
    if (state_.register_input_hex) {
      *value = string_util::from_string<uint32_t>(buffer, true);
    } else {
      *value = string_util::from_string<int32_t>(buffer);
    }
    any_changed = true;
  }
  ImGui::PopItemWidth();
  if (ImGui::IsItemHovered()) {
    auto alt_value = state_.register_input_hex
                         ? std::to_string(*value)
                         : string_util::to_hex_string(*value);
    ImGui::SetTooltip("%s", alt_value.c_str());
  }
  return any_changed;
}

bool DebugWindow::DrawRegisterTextBox(int id, uint64_t* value) {
  char buffer[256] = {0};
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank;
  if (state_.register_input_hex) {
    input_flags |= ImGuiInputTextFlags_CharsHexadecimal |
                   ImGuiInputTextFlags_AlwaysOverwrite |
                   ImGuiInputTextFlags_NoHorizontalScroll;
    auto src_value = xe::string_util::to_hex_string(*value);
    std::strcpy(buffer, src_value.c_str());
  } else {
    input_flags |=
        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll;
    auto src_value = std::to_string(*value);
    std::strcpy(buffer, src_value.c_str());
  }
  char label[16] = {0};
  std::snprintf(label, xe::countof(label), "##lregister%d", id);
  bool any_changed = false;
  ImGui::PushItemWidth(95);
  if (ImGui::InputText(label, buffer,
                       state_.register_input_hex ? 17 : sizeof(buffer),
                       input_flags)) {
    if (state_.register_input_hex) {
      *value = string_util::from_string<uint64_t>(buffer, true);
    } else {
      *value = string_util::from_string<int64_t>(buffer);
    }
    any_changed = true;
  }
  ImGui::PopItemWidth();
  if (ImGui::IsItemHovered()) {
    auto alt_value = state_.register_input_hex
                         ? std::to_string(*value)
                         : string_util::to_hex_string(*value);
    ImGui::SetTooltip("%s", alt_value.c_str());
  }
  return any_changed;
}

bool DebugWindow::DrawRegisterTextBox(int id, double* value) {
  char buffer[256] = {0};
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank;
  if (state_.register_input_hex) {
    input_flags |= ImGuiInputTextFlags_CharsHexadecimal |
                   ImGuiInputTextFlags_AlwaysOverwrite |
                   ImGuiInputTextFlags_NoHorizontalScroll;
    auto src_value = xe::string_util::to_hex_string(*value);
    std::strcpy(buffer, src_value.c_str());
  } else {
    input_flags |=
        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll;
    std::snprintf(buffer, xe::countof(buffer), "%.8F", *value);
  }
  char label[16] = {0};
  std::snprintf(label, xe::countof(label), "##dregister%d", id);
  bool any_changed = false;
  ImGui::PushItemWidth(95);
  if (ImGui::InputText(label, buffer,
                       state_.register_input_hex ? 17 : sizeof(buffer),
                       input_flags)) {
    if (state_.register_input_hex) {
      *value = string_util::from_string<double>(buffer, true);
    } else {
      *value = string_util::from_string<double>(buffer);
    }
    any_changed = true;
  }
  ImGui::PopItemWidth();
  if (ImGui::IsItemHovered()) {
    auto alt_value = state_.register_input_hex
                         ? std::to_string(*value)
                         : string_util::to_hex_string(*value);
    ImGui::SetTooltip("%s", alt_value.c_str());
  }
  return any_changed;
}

bool DebugWindow::DrawRegisterTextBoxes(int id, float* value) {
  char buffer[256] = {0};
  ImGuiInputTextFlags input_flags =
      ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank;
  if (state_.register_input_hex) {
    input_flags |= ImGuiInputTextFlags_CharsHexadecimal |
                   ImGuiInputTextFlags_AlwaysOverwrite |
                   ImGuiInputTextFlags_NoHorizontalScroll;
  } else {
    input_flags |=
        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll;
  }
  bool any_changed = false;
  char label[16] = {0};
  for (int i = 0; i < 4; ++i) {
    if (state_.register_input_hex) {
      auto src_value = xe::string_util::to_hex_string(value[i]);
      std::strcpy(buffer, src_value.c_str());
    } else {
      std::snprintf(buffer, xe::countof(buffer), "%F", value[i]);
    }
    std::snprintf(label, xe::countof(label), "##vregister%d_%d", id, i);
    ImGui::PushItemWidth(50);
    if (ImGui::InputText(label, buffer,
                         state_.register_input_hex ? 9 : sizeof(buffer),
                         input_flags)) {
      if (state_.register_input_hex) {
        value[i] = string_util::from_string<float>(buffer, true);
      } else {
        value[i] = string_util::from_string<float>(buffer);
      }
      any_changed = true;
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered()) {
      auto alt_value = state_.register_input_hex
                           ? std::to_string(value[i])
                           : string_util::to_hex_string(value[i]);
      ImGui::SetTooltip("%s", alt_value.c_str());
    }
    if (i < 3) {
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(1, 0));
      ImGui::SameLine();
    }
  }
  return any_changed;
}

void DebugWindow::DrawRegistersPane() {
  if (state_.register_group == RegisterGroup::kGuestGeneral) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::Button("GPR");
    ImGui::PopStyleColor();
  } else {
    if (ImGui::Button("GPR")) {
      state_.register_group = RegisterGroup::kGuestGeneral;
    }
  }
  ImGui::SameLine();
  if (state_.register_group == RegisterGroup::kGuestFloat) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::Button("FPR");
    ImGui::PopStyleColor();
  } else {
    if (ImGui::Button("FPR")) {
      state_.register_group = RegisterGroup::kGuestFloat;
    }
  }
  ImGui::SameLine();
  if (state_.register_group == RegisterGroup::kGuestVector) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::Button("VMX");
    ImGui::PopStyleColor();
  } else {
    if (ImGui::Button("VMX")) {
      state_.register_group = RegisterGroup::kGuestVector;
    }
  }
  ImGui::SameLine();
  if (state_.register_group == RegisterGroup::kHostGeneral) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::Button("x64");
    ImGui::PopStyleColor();
  } else {
    if (ImGui::Button("x64")) {
      state_.register_group = RegisterGroup::kHostGeneral;
    }
  }
  ImGui::SameLine();
  if (state_.register_group == RegisterGroup::kHostVector) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    ImGui::Button("XMM");
    ImGui::PopStyleColor();
  } else {
    if (ImGui::Button("XMM")) {
      state_.register_group = RegisterGroup::kHostVector;
    }
  }

  ImGui::Checkbox("Hex", &state_.register_input_hex);

  if (!state_.thread_info) {
    return;
  }
  auto thread_info = state_.thread_info;

  bool dirty_guest_context = false;
  bool dirty_host_context = false;
  switch (state_.register_group) {
    case RegisterGroup::kGuestGeneral: {
      if (!thread_info->guest_context.physical_membase) {
        return;
      }
      ImGui::BeginChild("##guest_general");
      ImGui::BeginGroup();
      ImGui::AlignTextToFramePadding();
      ImGui::Text(" lr");
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(4, 0));
      ImGui::SameLine();
      dirty_guest_context |=
          DrawRegisterTextBox(100, &thread_info->guest_context.lr);
      ImGui::EndGroup();
      ImGui::BeginGroup();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("ctr");
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(4, 0));
      ImGui::SameLine();
      dirty_guest_context |=
          DrawRegisterTextBox(101, &thread_info->guest_context.ctr);
      ImGui::EndGroup();
      // CR
      // XER
      // FPSCR
      // VSCR
      for (int i = 0; i < 32; ++i) {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(i < 10 ? " r%d" : "r%d", i);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        dirty_guest_context |=
            DrawRegisterTextBox(i, &thread_info->guest_context.r[i]);
        ImGui::EndGroup();
      }
      ImGui::EndChild();
    } break;
    case RegisterGroup::kGuestFloat: {
      if (!thread_info->guest_context.physical_membase) {
        return;
      }
      ImGui::BeginChild("##guest_float");
      for (int i = 0; i < 32; ++i) {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(i < 10 ? " f%d" : "f%d", i);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        dirty_guest_context |=
            DrawRegisterTextBox(i, &thread_info->guest_context.f[i]);
        ImGui::EndGroup();
      }
      ImGui::EndChild();
    } break;
    case RegisterGroup::kGuestVector: {
      if (!thread_info->guest_context.physical_membase) {
        return;
      }
      ImGui::BeginChild("##guest_vector");
      for (int i = 0; i < 128; ++i) {
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(i < 10 ? "  v%d" : (i < 100 ? " v%d" : "v%d"), i);
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        dirty_guest_context |=
            DrawRegisterTextBoxes(i, thread_info->guest_context.v[i].f32);
        ImGui::EndGroup();
      }
      ImGui::EndChild();
    } break;
    case RegisterGroup::kHostGeneral: {
      ImGui::BeginChild("##host_general");
      for (int i = 0; i < 18; ++i) {
        auto reg = static_cast<X64Register>(i);
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%3s", HostThreadContext::GetRegisterName(reg));
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        if (reg == X64Register::kRip) {
          dirty_guest_context |=
              DrawRegisterTextBox(i, &thread_info->host_context.rip);
        } else if (reg == X64Register::kEflags) {
          dirty_guest_context =
              DrawRegisterTextBox(i, &thread_info->host_context.eflags);
        } else {
          dirty_guest_context |= DrawRegisterTextBox(
              i, &thread_info->host_context.int_registers[i - 2]);
        }
        ImGui::EndGroup();
      }
      ImGui::EndChild();
    } break;
    case RegisterGroup::kHostVector: {
      ImGui::BeginChild("##host_vector");
      for (int i = 0; i < 16; ++i) {
        auto reg =
            static_cast<X64Register>(static_cast<int>(X64Register::kXmm0) + i);
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%5s", HostThreadContext::GetRegisterName(reg));
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        dirty_host_context |= DrawRegisterTextBoxes(
            i, thread_info->host_context.xmm_registers[i].f32);
        ImGui::EndGroup();
      }
      ImGui::EndChild();
    }
  }

  if (dirty_guest_context) {
    // TODO(benvanik): write context to thread.
    // NOTE: this will only work if context promotion is disabled!
  }
  if (dirty_host_context) {
    // TODO(benvanik): write context to thread.
  }
}

void DebugWindow::DrawThreadsPane() {
  ImGui::BeginGroup();
  //   checkbox to show host threads
  //   expand all toggle
  ImGui::EndGroup();
  ImGui::BeginChild("##threads_listing");
  for (size_t i = 0; i < cache_.thread_debug_infos.size(); ++i) {
    auto thread_info = cache_.thread_debug_infos[i];
    bool is_current_thread = thread_info == state_.thread_info;
    auto thread =
        emulator_->kernel_state()->GetThreadByID(thread_info->thread_id);
    if (!thread) {
      // TODO(benvanik): better display of zombie thread states.
      continue;
    }
    if (is_current_thread && state_.has_changed_thread) {
      ImGui::SetScrollHereY(0.5f);
      state_.has_changed_thread = false;
    }
    if (!is_current_thread) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
    } else {
      ImGui::PushStyleColor(ImGuiCol_Header,
                            ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
    }
    ImGui::PushID(thread_info);
    if (is_current_thread) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    const char* state_label = "?";
    if (thread->can_debugger_suspend()) {
      if (thread->is_running()) {
        if (thread->suspend_count() > 1) {
          state_label = "SUSPEND";
        } else {
          state_label = "RUNNING";
        }
      } else {
        state_label = "ZOMBIE";
      }
    }
    char thread_label[256];
    std::snprintf(thread_label, xe::countof(thread_label),
                  "%-5s %-7s id=%.4X hnd=%.4X   %s",
                  thread->is_guest_thread() ? "guest" : "host", state_label,
                  thread->thread_id(), thread->handle(),
                  thread->name().c_str());
    if (ImGui::CollapsingHeader(
            thread_label,
            is_current_thread ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      //   |     (log button) detail of kernel call categories
      // log button toggles only logging that thread
      ImGui::BulletText("Call Stack");
      ImGui::Indent();
      for (size_t j = 0; j < thread_info->frames.size(); ++j) {
        bool is_current_frame =
            is_current_thread && j == state_.thread_stack_frame_index;
        auto& frame = thread_info->frames[j];
        if (is_current_thread && !frame.guest_pc) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.6f));
        }
        char host_label[64];
        std::snprintf(host_label, xe::countof(host_label), "%016" PRIX64 "##%p",
                      frame.host_pc, &frame);
        if (ImGui::Selectable(host_label, is_current_frame,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          SelectThreadStackFrame(thread_info, j, true);
        }
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(8, 0));
        ImGui::SameLine();
        if (frame.guest_pc) {
          ImGui::Text("%08X", frame.guest_pc);
        } else {
          ImGui::Text("        ");
        }
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(8, 0));
        ImGui::SameLine();
        // breakpoints set? or something?
        ImGui::Text(" ");
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(8, 0));
        ImGui::SameLine();
        if (frame.guest_function) {
          ImGui::Text("%s", frame.guest_function->name().c_str());
        } else {
          ImGui::Text("%s", frame.name);
        }
        if (is_current_thread && !frame.guest_pc) {
          ImGui::PopStyleColor();
        }
      }
      ImGui::Unindent();
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  ImGui::EndChild();
}

void DebugWindow::DrawMemoryPane() {
  ImGui::Text("<memory>");
  // tools for searching:
  //   search bytes | text | pattern
  // https://github.com/ocornut/imgui/wiki/memory_editor_example
}

void DebugWindow::DrawBreakpointsPane() {
  auto& state = state_.breakpoints;

  ImGui::BeginChild("##toolbar", ImVec2(-1, 20));
  bool all_breakpoints_enabled = true;
  for (auto& breakpoint : state.all_breakpoints) {
    if (!breakpoint->is_enabled()) {
      all_breakpoints_enabled = false;
      break;
    }
  }
  if (ImGui::Checkbox("##toggle", &all_breakpoints_enabled)) {
    // Toggle all breakpoints on/off.
    // If any are not enabled we will enable all, otherwise disable all.
    for (auto& breakpoint : state.all_breakpoints) {
      breakpoint->set_enabled(all_breakpoints_enabled);
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Toggle all breakpoints.");
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(8, 0));
  ImGui::SameLine();
  if (ImGui::Button("+ Code")) {
    ImGui::OpenPopup("##add_code_breakpoint");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Add a code breakpoint for either PPC or x64.");
  }
  // TODO(benvanik): remove this set focus workaround when imgui is fixed:
  // https://github.com/ocornut/imgui/issues/343
  static int add_code_popup_render_count = 0;
  if (ImGui::BeginPopup("##add_code_breakpoint")) {
    ++add_code_popup_render_count;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("PPC");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();
    if (add_code_popup_render_count == 2) {
      ImGui::SetKeyboardFocusHere();
    }
    static char ppc_buffer[32] = {0};
    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_CharsUppercase |
                                      ImGuiInputTextFlags_CharsNoBlank |
                                      ImGuiInputTextFlags_CharsHexadecimal |
                                      ImGuiInputTextFlags_AlwaysOverwrite |
                                      ImGuiInputTextFlags_NoHorizontalScroll |
                                      ImGuiInputTextFlags_EnterReturnsTrue;
    ImGui::PushItemWidth(50);
    if (ImGui::InputText("##guest_address", ppc_buffer, 9, input_flags)) {
      uint32_t address = string_util::from_string<uint32_t>(ppc_buffer, true);
      ppc_buffer[0] = 0;
      CreateCodeBreakpoint(Breakpoint::AddressType::kGuest, address);
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 2));

    ImGui::AlignTextToFramePadding();
    ImGui::Text("x64");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();
    static char x64_buffer[64] = {0};
    ImGui::PushItemWidth(100);
    if (ImGui::InputText("##host_address", x64_buffer, 17, input_flags)) {
      uint64_t address = string_util::from_string<uint64_t>(x64_buffer, true);
      x64_buffer[0] = 0;
      CreateCodeBreakpoint(Breakpoint::AddressType::kHost, address);
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopItemWidth();

    ImGui::EndPopup();
  } else {
    add_code_popup_render_count = 0;
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(2, 0));
  ImGui::SameLine();

  if (ImGui::Button("+ Data")) {
    ImGui::OpenPopup("##add_data_breakpoint");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Add a memory data breakpoint.");
  }
  if (ImGui::BeginPopup("##add_data_breakpoint")) {
    ImGui::Text("TODO: data stuff");
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(2, 0));
  ImGui::SameLine();

  if (ImGui::Button("+ Kernel")) {
    ImGui::OpenPopup("##add_kernel_breakpoint");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Select active kernel breakpoints.");
  }
  // TODO(benvanik): remove this set focus workaround when imgui is fixed:
  // https://github.com/ocornut/imgui/issues/343
  static int kernel_popup_render_count = 0;
  if (ImGui::BeginPopup("##add_kernel_breakpoint")) {
    ++kernel_popup_render_count;

    auto export_tables = emulator_->export_resolver()->tables();

    ImGui::PushItemWidth(300);
    // TODO(benvanik): tag filtering.
    const char* its[] = {
        "All",
    };
    int ci = 0;
    ImGui::Combo("##kernel_categories", &ci, its, 1, 1);
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::BeginListBox("##kernel_calls", ImVec2(1000, 15));
    auto& all_exports = emulator_->export_resolver()->all_exports_by_name();
    auto call_rankings = xe::fuzzy_filter(state.kernel_call_filter, all_exports,
                                          offsetof(cpu::Export, name));
    bool has_any_call_filter = std::strlen(state.kernel_call_filter) > 0;
    if (has_any_call_filter) {
      std::sort(call_rankings.begin(), call_rankings.end(),
                [](std::pair<size_t, int>& a, std::pair<size_t, int>& b) {
                  if (a.second == b.second) {
                    return a.first > b.first;
                  } else {
                    return a.second > b.second;
                  }
                });
    }
    for (size_t i = 0; i < call_rankings.size(); ++i) {
      if (has_any_call_filter && !call_rankings[i].second) {
        continue;
      }
      auto export_entry = all_exports[call_rankings[i].first];
      if (export_entry->get_type() != cpu::Export::Type::kFunction ||
          !export_entry->is_implemented()) {
        continue;
      }
      // TODO(benvanik): skip unused kernel calls.
      ImGui::PushID(export_entry);
      // TODO(benvanik): selection, hover info (module name, ordinal, etc).
      bool is_pre_enabled = false;
      bool is_post_enabled = false;
      if (ImGui::Checkbox("##pre", &is_pre_enabled)) {
        // TODO(benvanik): add pre breakpoint (lookup thunk, add before
        //     syscall).
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Break immediately before this export is called.");
      }
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(1, 0));
      ImGui::SameLine();
      if (ImGui::Checkbox("##post", &is_post_enabled)) {
        // TODO(benvanik): add pre breakpoint (lookup thunk, add after
        //     syscall).
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Break immediately after this export returns.");
      }
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(4, 0));
      ImGui::SameLine();
      ImGui::Text("%s", export_entry->name);
      ImGui::Dummy(ImVec2(0, 1));
      ImGui::PopID();
    }
    ImGui::EndListBox();
    ImGui::Dummy(ImVec2(0, 3));
    if (kernel_popup_render_count == 2) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText(
        "##kernel_call_filter", state.kernel_call_filter,
        xe::countof(state.kernel_call_filter),
        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsNoBlank);
    ImGui::PopItemWidth();
    ImGui::EndPopup();
  } else {
    kernel_popup_render_count = 0;
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(2, 0));
  ImGui::SameLine();

  if (ImGui::Button("+ GPU")) {
    ImGui::OpenPopup("##add_gpu_breakpoint");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Select active GPU breakpoints.");
  }
  if (ImGui::BeginPopup("##add_gpu_breakpoint")) {
    ImGui::Text("TODO: swap, kick, etc");
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(8, 0));
  ImGui::SameLine();

  if (ImGui::Button("Clear")) {
    // Unregister and delete all breakpoints.
    while (!state.all_breakpoints.empty()) {
      DeleteCodeBreakpoint(state.all_breakpoints.front().get());
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Clear all breakpoints.");
  }
  ImGui::EndChild();
  ImGui::Separator();

  ImGui::PushItemWidth(-1);
  if (ImGui::BeginListBox("##empty",
                          ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
    std::vector<Breakpoint*> to_delete;
    for (auto& breakpoint : state.all_breakpoints) {
      ImGui::PushID(breakpoint.get());
      bool is_enabled = breakpoint->is_enabled();
      if (ImGui::Checkbox("##toggle", &is_enabled)) {
        breakpoint->set_enabled(is_enabled);
      }
      ImGui::SameLine();
      ImGui::Dummy(ImVec2(4, 0));
      ImGui::SameLine();
      auto breakpoint_str = breakpoint->to_string();
      bool is_selected = false;  // in function/stopped on line?
      if (ImGui::Selectable(breakpoint_str.c_str(), &is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        auto function = breakpoint->guest_function();
        assert_not_null(function);
        if (breakpoint->address_type() == Breakpoint::AddressType::kGuest) {
          NavigateToFunction(breakpoint->guest_function(),
                             breakpoint->guest_address(),
                             function->MapGuestAddressToMachineCode(
                                 breakpoint->guest_address()));
        } else {
          NavigateToFunction(function,
                             function->MapMachineCodeToGuestAddress(
                                 breakpoint->host_address()),
                             breakpoint->host_address());
        }
      }
      if (ImGui::BeginPopupContextItem("##breakpoint_context_menu")) {
        if (ImGui::MenuItem("Delete")) {
          to_delete.push_back(breakpoint.get());
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndListBox();
    if (!to_delete.empty()) {
      for (auto breakpoint : to_delete) {
        DeleteCodeBreakpoint(breakpoint);
      }
    }
  }
  ImGui::PopItemWidth();
}

void DebugWindow::DrawLogPane() {
  ImGui::Text("<log>");
  // bar:
  //   combo for log level
  //   check combo for areas (cpu/gpu/etc)
  //   combo for thread checkboxes
  //   filter text box
  //   ...
  //   copy visible button
  //   follow button
  //   clear button
  //   visible lines / total lines
  // log box:
  //   line per log line
  //   if big, click to open dialog with contents
}

void DebugWindow::SelectThreadStackFrame(cpu::ThreadDebugInfo* thread_info,
                                         size_t stack_frame_index,
                                         bool always_navigate) {
  state_.has_changed_thread = false;
  if (thread_info != state_.thread_info) {
    state_.has_changed_thread = true;
    state_.thread_info = thread_info;
  }
  if (state_.thread_info) {
    stack_frame_index =
        std::min(state_.thread_info->frames.size() - 1, stack_frame_index);
  }
  if (stack_frame_index != state_.thread_stack_frame_index) {
    state_.thread_stack_frame_index = stack_frame_index;
    state_.has_changed_thread = true;
  }
  if (state_.thread_info && state_.thread_info->frames.empty()) {
    return;
  }
  if (state_.thread_info) {
    auto new_host_pc =
        state_.thread_info->frames[state_.thread_stack_frame_index].host_pc;
    if (new_host_pc != state_.last_host_pc) {
      state_.last_host_pc = new_host_pc;
      state_.has_changed_pc = true;
    }
  }
  if ((always_navigate || state_.has_changed_thread) && state_.thread_info) {
    auto& frame = state_.thread_info->frames[state_.thread_stack_frame_index];
    if (frame.guest_function) {
      NavigateToFunction(frame.guest_function, frame.guest_pc, frame.host_pc);
    }
  }
}

void DebugWindow::NavigateToFunction(cpu::Function* function, uint32_t guest_pc,
                                     uint64_t host_pc) {
  state_.function = function;
  state_.last_host_pc = host_pc;
  state_.has_changed_pc = true;
}

void DebugWindow::UpdateCache() {
  auto kernel_state = emulator_->kernel_state();
  auto object_table = kernel_state->object_table();

  app_context_.CallInUIThread([this]() {
    std::string title = kBaseTitle;
    switch (processor_->execution_state()) {
      case cpu::ExecutionState::kEnded:
        title += " (ended)";
        break;
      case cpu::ExecutionState::kPaused:
        title += " (paused)";
        break;
      case cpu::ExecutionState::kRunning:
        title += " (running)";
        break;
      case cpu::ExecutionState::kStepping:
        title += " (stepping)";
        break;
    }
    window_->SetTitle(title);
  });

  cache_.is_running =
      processor_->execution_state() == cpu::ExecutionState::kRunning;
  if (cache_.is_running) {
    // Early exit - the rest of the data is kept stale on purpose.
    return;
  }

  // Fetch module listing.
  // We hold refs so that none are unloaded.
  cache_.modules =
      object_table->GetObjectsByType<XModule>(XObject::Type::Module);

  cache_.thread_debug_infos = processor_->QueryThreadDebugInfos();

  SelectThreadStackFrame(state_.thread_info, state_.thread_stack_frame_index,
                         false);
}

void DebugWindow::CreateCodeBreakpoint(Breakpoint::AddressType address_type,
                                       uint64_t address) {
  auto& state = state_.breakpoints;
  auto breakpoint = std::make_unique<Breakpoint>(
      processor_, address_type, address,
      [this](Breakpoint* breakpoint, cpu::ThreadDebugInfo* thread_info,
             uint64_t host_address) {
        OnBreakpointHit(breakpoint, thread_info);
      });
  if (breakpoint->address_type() == Breakpoint::AddressType::kGuest) {
    auto& map = state.code_breakpoints_by_guest_address;
    auto it = map.find(breakpoint->guest_address());
    if (it != map.end()) {
      // Already exists!
      return;
    }
    map.emplace(breakpoint->guest_address(), breakpoint.get());
  } else {
    auto& map = state.code_breakpoints_by_host_address;
    auto it = map.find(breakpoint->host_address());
    if (it != map.end()) {
      // Already exists!
      return;
    }
    map.emplace(breakpoint->host_address(), breakpoint.get());
  }
  processor_->AddBreakpoint(breakpoint.get());
  state.all_breakpoints.emplace_back(std::move(breakpoint));
}

void DebugWindow::DeleteCodeBreakpoint(Breakpoint* breakpoint) {
  auto& state = state_.breakpoints;
  for (size_t i = 0; i < state.all_breakpoints.size(); ++i) {
    if (state.all_breakpoints[i].get() != breakpoint) {
      continue;
    }
    processor_->RemoveBreakpoint(breakpoint);
    if (breakpoint->address_type() == Breakpoint::AddressType::kGuest) {
      auto& map = state.code_breakpoints_by_guest_address;
      auto it = map.find(breakpoint->guest_address());
      if (it != map.end()) {
        map.erase(it);
      }
    } else {
      auto& map = state.code_breakpoints_by_host_address;
      auto it = map.find(breakpoint->host_address());
      if (it != map.end()) {
        map.erase(it);
      }
    }
    state.all_breakpoints.erase(state.all_breakpoints.begin() + i);
    break;
  }
}

Breakpoint* DebugWindow::LookupBreakpointAtAddress(
    Breakpoint::AddressType address_type, uint64_t address) {
  auto& state = state_.breakpoints;
  if (address_type == Breakpoint::AddressType::kGuest) {
    auto& map = state.code_breakpoints_by_guest_address;
    auto it = map.find(static_cast<uint32_t>(address));
    return it == map.end() ? nullptr : it->second;
  } else {
    auto& map = state.code_breakpoints_by_host_address;
    auto it = map.find(static_cast<uintptr_t>(address));
    return it == map.end() ? nullptr : it->second;
  }
}

void DebugWindow::OnFocus() { Focus(); }

void DebugWindow::OnDetached() {
  UpdateCache();

  // Remove all breakpoints.
  while (!state_.breakpoints.all_breakpoints.empty()) {
    DeleteCodeBreakpoint(state_.breakpoints.all_breakpoints.front().get());
  }
}

void DebugWindow::OnExecutionPaused() {
  UpdateCache();
  Focus();
}

void DebugWindow::OnExecutionContinued() {
  UpdateCache();
  Focus();
}

void DebugWindow::OnExecutionEnded() {
  UpdateCache();
  Focus();
}

void DebugWindow::OnStepCompleted(cpu::ThreadDebugInfo* thread_info) {
  UpdateCache();
  SelectThreadStackFrame(thread_info, 0, true);
  Focus();
}

void DebugWindow::OnBreakpointHit(Breakpoint* breakpoint,
                                  cpu::ThreadDebugInfo* thread_info) {
  UpdateCache();
  SelectThreadStackFrame(thread_info, 0, true);
  Focus();
}

void DebugWindow::Focus() const {
  app_context_.CallInUIThread([this]() { window_->Focus(); });
}

}  // namespace ui
}  // namespace debug
}  // namespace xe
