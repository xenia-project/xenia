/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define MICRO_PROFILE_IMPL
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#include <xenia/profiling.h>

namespace xe {

std::unique_ptr<ProfilerDisplay> Profiler::display_ = nullptr;

void Profiler::Initialize() {
  MicroProfileInit();
  MicroProfileSetDisplayMode(2);
}

void Profiler::Dump() {
  MicroProfileDumpTimers();
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

void Profiler::ThreadExit() {
  MicroProfileOnThreadExit();
}

bool Profiler::OnKeyDown(int key_code) {
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd375731(v=vs.85).aspx
  switch (key_code) {
  case VK_TAB:
    MicroProfileToggleDisplayMode();
    return true;
  case VK_OEM_3: // `
    MicroProfileTogglePause();
    return true;
  case 0x31: // 1
    MicroProfileModKey(1);
    return true;
  }
  return false;
}

bool Profiler::OnKeyUp(int key_code) {
  switch (key_code) {
  case 0x31: // 1
    MicroProfileModKey(0);
    return true;
  }
  return false;
}

void Profiler::OnMouseDown(bool left_button, bool right_button) {
  MicroProfileMouseButton(left_button, right_button);
}

void Profiler::OnMouseUp() {
  MicroProfileMouseButton(0, 0);
}

void Profiler::OnMouseMove(int x, int y) {
  MicroProfileMousePosition(x, y, 0);
}

void Profiler::OnMouseWheel(int x, int y, int dy) {
  MicroProfileMousePosition(x, y, dy);
}

void Profiler::set_display(std::unique_ptr<ProfilerDisplay> display) {
  display_ = std::move(display);
}

void Profiler::Present() {
  MicroProfileFlip();
  if (!display_) {
    return;
  }

  display_->Begin();
  MicroProfileDraw(display_->width(), display_->height());
  display_->End();
}

}  // namespace xe

uint32_t MicroProfileGpuInsertTimeStamp() {
  return 0;
}

uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey) {
  return 0;
}

uint64_t MicroProfileTicksPerSecondGpu() {
  return 0;
}

const char* MicroProfileGetThreadName() {
  return "TODO: get thread name!";
}

void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor, MicroProfileBoxType type) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawBox(
      nX, nY, nX1, nY1,
      nColor,
      static_cast<xe::ProfilerDisplay::BoxType>(type));
}

void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices, uint32_t nColor) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawLine2D(nVertices, pVertices, nColor);
}

void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText, uint32_t nLen) {
  auto display = xe::Profiler::display();
  if (!display) {
    return;
  }
  display->DrawText(nX, nY, nColor, pText, nLen);
}
