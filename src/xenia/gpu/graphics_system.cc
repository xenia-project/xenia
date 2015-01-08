/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_system.h>

#include <poly/poly.h>
#include <xenia/emulator.h>
#include <xenia/cpu/processor.h>
#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {

GraphicsSystem::GraphicsSystem(Emulator* emulator)
    : emulator_(emulator),
      memory_(emulator->memory()),
      interrupt_callback_(0),
      interrupt_callback_data_(0) {}

GraphicsSystem::~GraphicsSystem() {}

X_STATUS GraphicsSystem::Setup() {
  processor_ = emulator_->processor();

  return X_STATUS_SUCCESS;
}

void GraphicsSystem::Shutdown() {}

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback(%.4X, %.4X)", callback, user_data);
}

void GraphicsSystem::DispatchInterruptCallback(uint32_t source, uint32_t cpu) {
  if (!interrupt_callback_) {
    return;
  }

  // Pick a CPU, if needed. We're going to guess 2. Because.
  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }

  XELOGGPU("Dispatching GPU interrupt at %.8X w/ mode %d on cpu %d",
           interrupt_callback_, source, cpu);

  // NOTE: we may be executing in some random thread.
  uint64_t args[] = {source, interrupt_callback_data_};
  processor_->ExecuteInterrupt(cpu, interrupt_callback_, args,
                               poly::countof(args));
}

}  // namespace gpu
}  // namespace xe
