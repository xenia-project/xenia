/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/nop/nop_graphics_system.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/nop/nop_graphics_driver.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;


namespace {

void __stdcall NopGraphicsSystemVsyncCallback(NopGraphicsSystem* gs, BOOLEAN) {
  gs->MarkVblank();
  gs->DispatchInterruptCallback(0);
}

}


NopGraphicsSystem::NopGraphicsSystem(Emulator* emulator) :
    GraphicsSystem(emulator),
    timer_queue_(NULL),
    vsync_timer_(NULL) {
}

NopGraphicsSystem::~NopGraphicsSystem() {
}

void NopGraphicsSystem::Initialize() {
  GraphicsSystem::Initialize();

  assert_null(driver_);
  driver_ = new NopGraphicsDriver(memory_);

  assert_null(timer_queue_);
  assert_null(vsync_timer_);

  timer_queue_ = CreateTimerQueue();
  CreateTimerQueueTimer(
      &vsync_timer_,
      timer_queue_,
      (WAITORTIMERCALLBACK)NopGraphicsSystemVsyncCallback,
      this,
      16,
      100,
      WT_EXECUTEINTIMERTHREAD);
}

void NopGraphicsSystem::Pump() {
}

void NopGraphicsSystem::Shutdown() {
  if (vsync_timer_) {
    DeleteTimerQueueTimer(timer_queue_, vsync_timer_, NULL);
  }
  if (timer_queue_) {
    DeleteTimerQueueEx(timer_queue_, NULL);
  }

  GraphicsSystem::Shutdown();
}
