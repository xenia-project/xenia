/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define MICROPROFILE_IMPL
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#define MICROPROFILE_PRINTF PLOGI
#include <microprofile/microprofile.h>

#include <xenia/profiling.h>

#if XE_OPTION_PROFILING_UI
#include <microprofile/microprofileui.h>
#endif  // XE_OPTION_PROFILING_UI

namespace xe {

std::unique_ptr<ProfilerDisplay> Profiler::display_ = nullptr;

#if XE_OPTION_PROFILING

void Profiler::Initialize() {
  MicroProfileSetForceEnable(true);
  MicroProfileSetEnableAllGroups(true);
  MicroProfileSetForceMetaCounters(true);
#if XE_OPTION_PROFILING_UI
  MicroProfileInitUI();
  MicroProfileSetDisplayMode(1);
#endif  // XE_OPTION_PROFILING_UI
}

void Profiler::Dump() {
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

#else

void Profiler::OnMouseDown(bool left_button, bool right_button) {}

void Profiler::OnMouseUp() {}

void Profiler::OnMouseMove(int x, int y) {}

void Profiler::OnMouseWheel(int x, int y, int dy) {}

#endif  // XE_OPTION_PROFILING_UI

void Profiler::set_display(std::unique_ptr<ProfilerDisplay> display) {
  display_ = std::move(display);
}

void Profiler::Present() {
  MicroProfileFlip();
#if XE_OPTION_PROFILING_UI
  if (!display_) {
    return;
  }
  float left = 0.f;
  float right = display_->width();
  float bottom = display_->height();
  float top = 0.f;
  float near = -1.f;
  float far = 1.f;
  float projection[16] = {0};
  projection[0] = 2.0f / (right - left);
  projection[5] = 2.0f / (top - bottom);
  projection[10] = -2.0f / (far - near);
  projection[12] = -(right + left) / (right - left);
  projection[13] = -(top + bottom) / (top - bottom);
  projection[14] = -(far + near) / (far - near);
  projection[15] = 1.f;
  display_->Begin();
  MicroProfileBeginDraw(display_->width(), display_->height(), projection);
  MicroProfileDraw(display_->width(), display_->height());
  MicroProfileEndDraw();
  display_->End();
#endif  // XE_OPTION_PROFILING_UI
}

#else

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
