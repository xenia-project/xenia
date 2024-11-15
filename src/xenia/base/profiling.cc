/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Add this to the beginning of your source file or a common header
typedef unsigned int u_int;
typedef unsigned char u_char;
typedef unsigned short u_short;

#include <algorithm>
#include <string>

// NOTE: this must be included before microprofile as macro expansion needs
// XELOGI.
#include "xenia/base/logging.h"

#include "third_party/fmt/include/fmt/printf.h"

// NOTE: microprofile must be setup first, before profiling.h is included.
#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 10)
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#define MICROPROFILE_WEBSERVER_MAXFRAMES 3
#define MICROPROFILE_PRINTF(...)                               \
  do {                                                         \
    auto xenia_profiler_formatted = fmt::sprintf(__VA_ARGS__); \
    XELOGI("{}", xenia_profiler_formatted);                    \
  } while (false);
#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_DEBUG 0
#define MICROPROFILE_MAX_THREADS 128
#include "third_party/microprofile/microprofile.h"

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/ui_event.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"

#if XE_OPTION_PROFILING
#include "third_party/microprofile/microprofileui.h"
#endif  // XE_OPTION_PROFILING

#if XE_OPTION_PROFILING_UI
#include "xenia/ui/microprofile_drawer.h"
#endif  // XE_OPTION_PROFILING_UI

DEFINE_bool(profiler_dpi_scaling, false,
            "Apply window DPI scaling to the profiler.", "UI");
DEFINE_bool(show_profiler, false, "Show profiling UI by default.", "UI");

namespace xe {

#if XE_OPTION_PROFILING

Profiler::ProfilerWindowInputListener Profiler::input_listener_;
size_t Profiler::z_order_ = 0;
ui::Window* Profiler::window_ = nullptr;
#if XE_OPTION_PROFILING_UI
Profiler::ProfilerUIDrawer Profiler::ui_drawer_;
ui::Presenter* Profiler::presenter_ = nullptr;
std::unique_ptr<ui::MicroprofileDrawer> Profiler::drawer_;
bool Profiler::dpi_scaling_ = false;
#endif  // XE_OPTION_PROFILING_UI

bool Profiler::is_enabled() { return true; }

bool Profiler::is_visible() { return is_enabled() && MicroProfileIsDrawing(); }

void Profiler::Initialize() {
  // Custom groups.
  MicroProfileSetEnableAllGroups(false);
  MicroProfileForceEnableGroup("apu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("cpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("gpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("internal", MicroProfileTokenTypeCpu);
  g_MicroProfile.nGroupMask = g_MicroProfile.nForceGroup;
  g_MicroProfile.nActiveGroup = g_MicroProfile.nActiveGroupWanted =
      g_MicroProfile.nGroupMask;

  // Custom timers: time, average.
  g_MicroProfile.nBars |= 0x1 | 0x2;
  g_MicroProfile.nActiveBars |= 0x1 | 0x2;

#if XE_OPTION_PROFILING_UI
  dpi_scaling_ = cvars::profiler_dpi_scaling;
  MicroProfileInitUI();
  g_MicroProfileUI.bShowSpikes = true;
  g_MicroProfileUI.nOpacityBackground = 0x40u << 24;
  g_MicroProfileUI.nOpacityForeground = 0xc0u << 24;
  if (cvars::show_profiler) {
    MicroProfileSetDisplayMode(1);
  }
#else
  MicroProfileSetForceEnable(true);
  MicroProfileSetEnableAllGroups(true);
  MicroProfileSetForceMetaCounters(false);
#endif  // XE_OPTION_PROFILING_UI
}

void Profiler::Dump() {
#if XE_OPTION_PROFILING_UI
  MicroProfileDumpTimers();
#endif  // XE_OPTION_PROFILING_UI
  // MicroProfileDumpHtml("profile.html");
  // MicroProfileDumpHtmlToFile();
}

void Profiler::Shutdown() {
  SetUserIO(0, nullptr, nullptr, nullptr);
  window_ = nullptr;
  MicroProfileShutdown();
}

uint32_t Profiler::GetColor(const char* str) {
  std::hash<std::string> fn;
  size_t value = fn(str);
  return value & 0xFFFFFF;
}

void Profiler::ThreadEnter(const char* name) {
  MicroProfileOnThreadCreate(name);
}

void Profiler::ThreadExit() { MicroProfileOnThreadExit(); }

void Profiler::ProfilerWindowInputListener::OnKeyDown(ui::KeyEvent& e) {
  // https://msdn.microsoft.com/en-us/library/windows/desktop/dd375731(v=vs.85).aspx
  bool handled = true;
  switch (e.virtual_key()) {
    case ui::VirtualKey::kOem3:  // `
      MicroProfileTogglePause();
      break;
#if XE_OPTION_PROFILING_UI
    case ui::VirtualKey::kTab:
      ToggleDisplay();
      break;
    case ui::VirtualKey::k1:
      MicroProfileModKey(1);
      break;
#endif  // XE_OPTION_PROFILING_UI
    default:
      handled = false;
      break;
  }
  if (handled) {
    e.set_handled(true);
  }
  PostInputEvent();
}

void Profiler::ProfilerWindowInputListener::OnKeyUp(ui::KeyEvent& e) {
  bool handled = true;
  switch (e.virtual_key()) {
#if XE_OPTION_PROFILING_UI
    case ui::VirtualKey::k1:
      MicroProfileModKey(0);
      break;
#endif  // XE_OPTION_PROFILING_UI
    default:
      handled = false;
      break;
  }
  if (handled) {
    e.set_handled(true);
  }
  PostInputEvent();
}

#if XE_OPTION_PROFILING_UI

void Profiler::ProfilerWindowInputListener::OnMouseDown(ui::MouseEvent& e) {
  Profiler::SetMousePosition(e.x(), e.y(), 0);
  MicroProfileMouseButton(e.button() == ui::MouseEvent::Button::kLeft,
                          e.button() == ui::MouseEvent::Button::kRight);
  e.set_handled(true);
  PostInputEvent();
}

void Profiler::ProfilerWindowInputListener::OnMouseUp(ui::MouseEvent& e) {
  Profiler::SetMousePosition(e.x(), e.y(), 0);
  MicroProfileMouseButton(0, 0);
  e.set_handled(true);
  PostInputEvent();
}

void Profiler::ProfilerWindowInputListener::OnMouseMove(ui::MouseEvent& e) {
  Profiler::SetMousePosition(e.x(), e.y(), 0);
  e.set_handled(true);
  PostInputEvent();
}

void Profiler::ProfilerWindowInputListener::OnMouseWheel(ui::MouseEvent& e) {
  Profiler::SetMousePosition(e.x(), e.y(), e.scroll_y());
  e.set_handled(true);
  PostInputEvent();
}

void Profiler::TogglePause() { MicroProfileTogglePause(); }

#else

void Profiler::TogglePause() {}

#endif  // XE_OPTION_PROFILING_UI

void Profiler::ToggleDisplay() {
  bool was_visible = is_visible();
  MicroProfileToggleDisplayMode();
  if (is_visible() != was_visible) {
    if (window_) {
      if (was_visible) {
        window_->RemoveInputListener(&input_listener_);
      } else {
        window_->AddInputListener(&input_listener_, z_order_);
      }
    }
#if XE_OPTION_PROFILING_UI
    if (presenter_) {
      if (was_visible) {
        presenter_->RemoveUIDrawerFromUIThread(&ui_drawer_);
      } else {
        presenter_->AddUIDrawerFromUIThread(&ui_drawer_, z_order_);
      }
    }
#endif  // XE_OPTION_PROFILING_UI
  }
}

void Profiler::SetUserIO(size_t z_order, ui::Window* window,
                         ui::Presenter* presenter,
                         ui::ImmediateDrawer* immediate_drawer) {
#if XE_OPTION_PROFILING_UI
  if (presenter_ && is_visible()) {
    presenter_->RemoveUIDrawerFromUIThread(&ui_drawer_);
  }
  drawer_.reset();
  presenter_ = nullptr;
#endif  // XE_OPTION_PROFILING_UI

  if (window_) {
    if (is_visible()) {
      window_->RemoveInputListener(&input_listener_);
    }
    window_ = nullptr;
  }

  if (!window) {
    return;
  }

  z_order_ = z_order;
  window_ = window;

#if XE_OPTION_PROFILING_UI
  if (presenter && immediate_drawer) {
    presenter_ = presenter;
    drawer_ = std::make_unique<ui::MicroprofileDrawer>(immediate_drawer);
  }
#endif  // XE_OPTION_PROFILING_UI

  if (is_visible()) {
    window_->AddInputListener(&input_listener_, z_order_);
#if XE_OPTION_PROFILING_UI
    if (presenter_) {
      presenter_->AddUIDrawerFromUIThread(&ui_drawer_, z_order_);
    }
#endif  // XE_OPTION_PROFILING_UI
  }
}

void Profiler::Flip() {
  MicroProfileFlip();
  // This can be called from non-UI threads, so not trying to access the drawer
  // to trigger redraw here as it's owned and managed exclusively by the UI
  // thread. Relying on continuous painting currently.
}

#if XE_OPTION_PROFILING_UI
void Profiler::ProfilerUIDrawer::Draw(ui::UIDrawContext& ui_draw_context) {
  if (!window_ || !presenter_ || !drawer_) {
    return;
  }
  SCOPE_profile_cpu_f("internal");
  uint32_t coordinate_space_width = dpi_scaling_
                                        ? window_->GetActualLogicalWidth()
                                        : window_->GetActualPhysicalWidth();
  uint32_t coordinate_space_height = dpi_scaling_
                                         ? window_->GetActualLogicalHeight()
                                         : window_->GetActualPhysicalHeight();
  drawer_->Begin(ui_draw_context, coordinate_space_width,
                 coordinate_space_height);
  MicroProfileDraw(coordinate_space_width, coordinate_space_height);
  drawer_->End();
  // Continuous repaint.
  if (is_visible()) {
    presenter_->RequestUIPaintFromUIThread();
  }
}
#endif  // XE_OPTION_PROFILING_UI

#if XE_OPTION_PROFILING_UI
void Profiler::SetMousePosition(int32_t x, int32_t y, int32_t wheel_delta) {
  if (!window_) {
    return;
  }
  if (dpi_scaling_) {
    x = window_->PositionToLogical(x);
    y = window_->PositionToLogical(y);
  }
  MicroProfileMousePosition(uint32_t(std::max(int32_t(0), x)),
                            uint32_t(std::max(int32_t(0), y)), wheel_delta);
}
#endif  // XE_OPTION_PROFILING_UI

void Profiler::PostInputEvent() {
  // The profiler can be hidden from within the profiler (Mode > Off).
  if (!is_visible()) {
    window_->RemoveInputListener(&input_listener_);
#if XE_OPTION_PROFILING_UI
    if (presenter_) {
      presenter_->RemoveUIDrawerFromUIThread(&ui_drawer_);
    }
#endif  // XE_OPTION_PROFILING_UI
    return;
  }
  // Relying on continuous painting currently, no need to request drawing.
}

#else

bool Profiler::is_enabled() { return false; }
bool Profiler::is_visible() { return false; }
void Profiler::Initialize() {}
void Profiler::Dump() {}
void Profiler::Shutdown() {}
uint32_t Profiler::GetColor(const char* str) { return 0; }
void Profiler::ThreadEnter(const char* name) {}
void Profiler::ThreadExit() {}
void Profiler::ToggleDisplay() {}
void Profiler::TogglePause() {}
void Profiler::SetUserIO(size_t z_order, ui::Window* window,
                         ui::Presenter* presenter,
                         ui::ImmediateDrawer* immediate_drawer) {}
void Profiler::Flip() {}

#endif  // XE_OPTION_PROFILING

}  // namespace xe

uint32_t MicroProfileGpuInsertTimeStamp() { return 0; }

const char* MicroProfileGetThreadName() { return "TODO: get thread name!"; }

#if XE_OPTION_PROFILING
#if XE_OPTION_PROFILING_UI

void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor,
                         MicroProfileBoxType type) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawBox(nX, nY, nX1, nY1, nColor,
                  static_cast<xe::ui::MicroprofileDrawer::BoxType>(type));
}

void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices,
                            uint32_t nColor) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawLine2D(nVertices, pVertices, nColor);
}

void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText,
                          uint32_t nLen) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawTextString(nX, nY, nColor, pText, nLen);
}

#endif  // XE_OPTION_PROFILING_UI

#endif  // XE_OPTION_PROFILING
