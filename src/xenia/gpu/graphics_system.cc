/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_system.h>

#include <xenia/cpu/processor.h>
#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/ring_buffer_worker.h>
#include <xenia/gpu/xenos/registers.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


GraphicsSystem::GraphicsSystem(const CreationParams* params) :
    interrupt_callback_(0), interrupt_callback_data_(0),
    swap_pending_(false) {
  memory_ = xe_memory_retain(params->memory);

  worker_ = new RingBufferWorker(memory_);

  // Set during Initialize();
  driver_ = 0;

  // Create the run loop used for any windows/etc.
  // This must be done on the thread we create the driver.
  run_loop_ = xe_run_loop_create();

  // Create worker thread.
  // This will initialize the graphics system.
  // Init needs to happen there so that any thread-local stuff
  // is created on the right thread.
  running_ = true;
  thread_ = xe_thread_create(
      "GraphicsSystem",
      (xe_thread_callback)ThreadStartThunk, this);
  xe_thread_start(thread_);
}

GraphicsSystem::~GraphicsSystem() {
  // TODO(benvanik): thread join.
  running_ = false;
  xe_thread_release(thread_);

  // TODO(benvanik): worker join/etc.
  delete worker_;

  xe_run_loop_release(run_loop_);
  xe_memory_release(memory_);
}

xe_memory_ref GraphicsSystem::memory() {
  return xe_memory_retain(memory_);
}

shared_ptr<cpu::Processor> GraphicsSystem::processor() {
  return processor_;
}

void GraphicsSystem::set_processor(shared_ptr<cpu::Processor> processor) {
  processor_ = processor;
}

void GraphicsSystem::ThreadStart() {
  xe_run_loop_ref run_loop = xe_run_loop_retain(run_loop_);

  // Initialize driver and ringbuffer.
  Initialize();
  XEASSERTNOTNULL(driver_);

  // Main run loop.
  while (running_) {
    // Peek main run loop.
    if (xe_run_loop_pump(run_loop)) {
      break;
    }
    if (!running_) {
      break;
    }

    // Pump worker.
    worker_->Pump();

    // Pump graphics system.
    Pump();
  }
  running_ = false;

  Shutdown();

  xe_run_loop_release(run_loop);

  // TODO(benvanik): call module API to kill?
}

void GraphicsSystem::Initialize() {
}

void GraphicsSystem::Shutdown() {
}

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback(%.4X, %.4X)", callback, user_data);
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  XEASSERTNOTNULL(driver_);
  worker_->Initialize(driver_, ptr, page_count);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size) {
  worker_->EnableReadPointerWriteBack(ptr, block_size);
}

uint64_t GraphicsSystem::ReadRegister(uint32_t r) {
  XELOGGPU("ReadRegister(%.4X)", r);

  RegisterFile* regs = driver_->register_file();

  switch (r) {
  case 0x6544: // ? vblank pending?
    return 1;
  }

  XEASSERT(r >= 0 && r < kXEGpuRegisterCount);
  return regs->values[r].u32;
}

void GraphicsSystem::WriteRegister(uint32_t r, uint64_t value) {
  XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);

  RegisterFile* regs = driver_->register_file();

  switch (r) {
    case 0x0714: // CP_RB_WPTR
      worker_->UpdateWritePointer((uint32_t)value);
      break;
  }

  XEASSERT(r >= 0 && r < kXEGpuRegisterCount);
  XELOGW("Unknown GPU register %.4X write: %.8X", r, value);
  regs->values[r].u32 = (uint32_t)value;
}

void GraphicsSystem::DispatchInterruptCallback() {
  // NOTE: we may be executing in some random thread.
  if (!interrupt_callback_) {
    return;
  }
  processor_->ExecuteInterrupt(
      interrupt_callback_, 0, interrupt_callback_data_);
}
