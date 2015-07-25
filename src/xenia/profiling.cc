/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <gflags/gflags.h>

#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 10)
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#define MICROPROFILE_WEBSERVER_MAXFRAMES 3
#define MICROPROFILE_PRINTF XELOGI
#define MICROPROFILE_WEBSERVER 1
#define MICROPROFILE_DEBUG 0
#if MICROPROFILE_WEBSERVER
#include <winsock.h>
#endif  // MICROPROFILE_WEBSERVER
#include <microprofile/microprofile.h>
#include <microprofile/microprofileui.h>

#include "xenia/profiling.h"

DEFINE_bool(show_profiler, false, "Show profiling UI by default.");

namespace xe {

std::unique_ptr<ProfilerDisplay> Profiler::display_ = nullptr;

#if XE_OPTION_PROFILING

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
  MicroProfileInitUI();
  g_MicroProfileUI.bShowSpikes = true;
  g_MicroProfileUI.nOpacityBackground = 0x40u << 24;
  g_MicroProfileUI.nOpacityForeground = 0xc0u << 24;
  if (FLAGS_show_profiler) {
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
  MicroProfileDumpHtml("profile.html");
  MicroProfileDumpHtmlToFile();
}

void Profiler::Shutdown() {
  display_.reset();
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

bool Profiler::OnKeyDown(int key_code) {
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd375731(v=vs.85).aspx
  switch (key_code) {
    case VK_OEM_3:  // `
      MicroProfileTogglePause();
      return true;
#if XE_OPTION_PROFILING_UI
    case VK_TAB:
      MicroProfileToggleDisplayMode();
      return true;
    case 0x31:  // 1
      MicroProfileModKey(1);
      return true;
#endif  // XE_OPTION_PROFILING_UI
  }
  return false;
}

bool Profiler::OnKeyUp(int key_code) {
  switch (key_code) {
#if XE_OPTION_PROFILING_UI
    case 0x31:  // 1
      MicroProfileModKey(0);
      return true;
#endif  // XE_OPTION_PROFILING_UI
  }
  return false;
}

#if XE_OPTION_PROFILING_UI

void Profiler::OnMouseDown(bool left_button, bool right_button) {
  MicroProfileMouseButton(left_button, right_button);
}

void Profiler::OnMouseUp() { MicroProfileMouseButton(0, 0); }

void Profiler::OnMouseMove(int x, int y) { MicroProfileMousePosition(x, y, 0); }

void Profiler::OnMouseWheel(int x, int y, int dy) {
  MicroProfileMousePosition(x, y, dy);
}

void Profiler::ToggleDisplay() { MicroProfileToggleDisplayMode(); }

void Profiler::TogglePause() { MicroProfileTogglePause(); }

#else

void Profiler::OnMouseDown(bool left_button, bool right_button) {}

void Profiler::OnMouseUp() {}

void Profiler::OnMouseMove(int x, int y) {}

void Profiler::OnMouseWheel(int x, int y, int dy) {}

void Profiler::ToggleDisplay() {}

void Profiler::TogglePause() {}

#endif  // XE_OPTION_PROFILING_UI

void Profiler::set_display(std::unique_ptr<ProfilerDisplay> display) {
  display_ = std::move(display);
}

void Profiler::Present() {
  SCOPE_profile_cpu_f("internal");
  MicroProfileFlip();
#if XE_OPTION_PROFILING_UI
  if (!display_) {
    return;
  }
  display_->Begin();
  MicroProfileDraw(display_->width(), display_->height());
  display_->End();
#endif  // XE_OPTION_PROFILING_UI
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
bool Profiler::OnKeyDown(int key_code) { return false; }
bool Profiler::OnKeyUp(int key_code) { return false; }
void Profiler::OnMouseDown(bool left_button, bool right_button) {}
void Profiler::OnMouseUp() {}
void Profiler::OnMouseMove(int x, int y) {}
void Profiler::OnMouseWheel(int x, int y, int dy) {}
void Profiler::set_display(std::unique_ptr<ProfilerDisplay> display) {}
void Profiler::Present() {}

#endif  // XE_OPTION_PROFILING

}  // namespace xe

#if XE_OPTION_PROFILING

uint32_t MicroProfileGpuInsertTimeStamp() { return 0; }

uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey) { return 0; }

uint64_t MicroProfileTicksPerSecondGpu() { return 0; }

const char* MicroProfileGetThreadName() { return "TODO: get thread name!"; }

#if XE_OPTION_PROFILING_UI

void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor,
                         MicroProfileBoxType type) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawBox(nX, nY, nX1, nY1, nColor,
                   static_cast<xe::ProfilerDisplay::BoxType>(type));
}

void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices,
                            uint32_t nColor) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawLine2D(nVertices, pVertices, nColor);
}

void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText,
                          uint32_t nLen) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawText(nX, nY, nColor, pText, nLen);
}

#endif  // XE_OPTION_PROFILING_UI

#endif  // XE_OPTION_PROFILING
