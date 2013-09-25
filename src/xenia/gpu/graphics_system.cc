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
#include <xenia/gpu/ring_buffer_worker.h>


using namespace xe;
using namespace xe::gpu;


GraphicsSystem::GraphicsSystem(const CreationParams* params) :
    interrupt_callback_(0), interrupt_callback_data_(0) {
  memory_ = xe_memory_retain(params->memory);

  worker_ = new RingBufferWorker(memory_);
}

GraphicsSystem::~GraphicsSystem() {
  // TODO(benvanik): worker join/etc.
  delete worker_;

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

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback(%.4X, %.4X)", callback, user_data);
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  worker_->Initialize(ptr, page_count);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size) {
  worker_->EnableReadPointerWriteBack(ptr, block_size);
}

uint64_t GraphicsSystem::ReadRegister(uint32_t r) {
  XELOGGPU("ReadRegister(%.4X)", r);

  switch (r) {
  case 0x6544: // ? vblank pending?
    return 1;
  }

  return 0;
}

void GraphicsSystem::WriteRegister(uint32_t r, uint64_t value) {
  XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);

  switch (r) {
    case 0x0714: // CP_RB_WPTR
      worker_->UpdateWritePointer((uint32_t)value);
      break;

    default:
      XELOGW("Unknown GPU register %.4X write: %.8X", r, value);
      break;
  }
}

void GraphicsSystem::DispatchInterruptCallback() {
  // NOTE: we may be executing in some random thread.
  if (!interrupt_callback_) {
    return;
  }
  processor_->ExecuteInterrupt(
      interrupt_callback_, 0, interrupt_callback_data_);
}
