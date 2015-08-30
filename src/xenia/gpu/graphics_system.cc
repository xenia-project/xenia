/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/graphics_system.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/kernel/objects/xthread.h"

namespace xe {
namespace gpu {

namespace gl4 {
std::unique_ptr<GraphicsSystem> Create(Emulator* emulator);
}  // namespace gl4

std::unique_ptr<GraphicsSystem> GraphicsSystem::Create(Emulator* emulator) {
  if (FLAGS_gpu.compare("gl4") == 0) {
    return xe::gpu::gl4::Create(emulator);
  } else {
    // Create best available.
    std::unique_ptr<GraphicsSystem> best;

    best = xe::gpu::gl4::Create(emulator);
    if (best) {
      return best;
    }

    // Nothing!
    return nullptr;
  }
}

GraphicsSystem::GraphicsSystem(Emulator* emulator) : emulator_(emulator) {}

GraphicsSystem::~GraphicsSystem() = default;

X_STATUS GraphicsSystem::Setup(cpu::Processor* processor, ui::Loop* target_loop,
                               ui::Window* target_window) {
  processor_ = processor;
  memory_ = processor->memory();
  target_loop_ = target_loop;
  target_window_ = target_window;

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

  auto thread = kernel::XThread::GetCurrentThread();
  assert_not_null(thread);

  // Pick a CPU, if needed. We're going to guess 2. Because.
  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }
  thread->SetActiveCpu(cpu);

  XELOGGPU("Dispatching GPU interrupt at %.8X w/ mode %d on cpu %d",
           interrupt_callback_, source, cpu);

  uint64_t args[] = {source, interrupt_callback_data_};
  processor_->Execute(thread->thread_state(), interrupt_callback_, args,
                      xe::countof(args));
}

}  // namespace gpu
}  // namespace xe
