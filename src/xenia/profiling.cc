/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define MICRO_PROFILE_IMPL
#include <xenia/profiling.h>

namespace xe {

std::unique_ptr<ProfilerDisplay> Profiler::display_ = nullptr;

void Profiler::Dump() {
  MicroProfileDumpTimers();
}

void Profiler::Shutdown() {
  display_.reset();
  MicroProfileShutdown();
}

void Profiler::ThreadEnter(const char* name) {
  MicroProfileOnThreadCreate(name);
}

void Profiler::ThreadExit() {
  MicroProfileOnThreadExit();
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
