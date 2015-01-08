/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_graphics_system.h>

#include <poly/threading.h>
#include <xenia/cpu/processor.h>
#include <xenia/gpu/gl4/gl4_gpu-private.h>
#include <xenia/gpu/gl4/gl4_profiler_display.h>
#include <xenia/gpu/gpu-private.h>

namespace xe {
namespace gpu {
namespace gl4 {

extern "C" GLEWContext* glewGetContext();

GL4GraphicsSystem::GL4GraphicsSystem(Emulator* emulator)
    : GraphicsSystem(emulator), timer_queue_(nullptr), vsync_timer_(nullptr) {}

GL4GraphicsSystem::~GL4GraphicsSystem() = default;

X_STATUS GL4GraphicsSystem::Setup() {
  auto result = GraphicsSystem::Setup();
  if (result) {
    return result;
  }

  // Create rendering control.
  // This must happen on the UI thread.
  poly::threading::Fence control_ready_fence;
  auto loop = emulator_->main_window()->loop();
  std::unique_ptr<GLContext> processor_context;
  loop->Post([&]() {
    // Setup the GL control that actually does the drawing.
    // We run here in the loop and only touch it (and its context) on this
    // thread. That means some sync-fu when we want to swap.
    control_ = std::make_unique<WGLControl>(loop);
    emulator_->main_window()->AddChild(control_.get());

    // Setup the GL context the command processor will do all its drawing in.
    // It's shared with the control context so that we can resolve framebuffers
    // from it.
    processor_context = control_->context()->CreateShared();

    {
      GLContextLock context_lock(control_->context());
      auto profiler_display =
          std::make_unique<GL4ProfilerDisplay>(control_.get());
      Profiler::set_display(std::move(profiler_display));
    }

    control_ready_fence.Signal();
  });
  control_ready_fence.Wait();

  // Create command processor. This will spin up a thread to process all
  // incoming ringbuffer packets.
  command_processor_ = std::make_unique<CommandProcessor>(this);
  if (!command_processor_->Initialize(std::move(processor_context))) {
    PLOGE("Unable to initialize command processor");
    return X_STATUS_UNSUCCESSFUL;
  }
  command_processor_->set_swap_handler(
      [this](const SwapParameters& swap_params) { SwapHandler(swap_params); });

  // Let the processor know we want register access callbacks.
  emulator_->memory()->AddMappedRange(
      0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // 60hz vsync timer.
  DWORD timer_period = 16;
  if (!FLAGS_vsync) {
    // DANGER a value too low here will lead to starvation!
    timer_period = 4;
  }
  timer_queue_ = CreateTimerQueue();
  CreateTimerQueueTimer(&vsync_timer_, timer_queue_,
                        (WAITORTIMERCALLBACK)VsyncCallbackThunk, this, 16,
                        timer_period, WT_EXECUTEINPERSISTENTTHREAD);

  return X_STATUS_SUCCESS;
}

void GL4GraphicsSystem::Shutdown() {
  DeleteTimerQueueTimer(timer_queue_, vsync_timer_, nullptr);
  DeleteTimerQueue(timer_queue_);

  command_processor_->Shutdown();

  // TODO(benvanik): remove mapped range.

  command_processor_.reset();
  control_.reset();

  GraphicsSystem::Shutdown();
}

void GL4GraphicsSystem::InitializeRingBuffer(uint32_t ptr,
                                             uint32_t page_count) {
  command_processor_->InitializeRingBuffer(ptr, page_count);
}

void GL4GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                   uint32_t block_size) {
  command_processor_->EnableReadPointerWriteBack(ptr, block_size);
}

void GL4GraphicsSystem::MarkVblank() {
  static bool thread_name_set = false;
  if (!thread_name_set) {
    thread_name_set = true;
    Profiler::ThreadEnter("GL4 Vsync Timer");
  }
  SCOPE_profile_cpu_f("gpu");

  // Increment vblank counter (so the game sees us making progress).
  command_processor_->increment_counter();

  // TODO(benvanik): we shouldn't need to do the dispatch here, but there's
  //     something wrong and the CP will block waiting for code that
  //     needs to be run in the interrupt.
  DispatchInterruptCallback(0, 2);
}

void GL4GraphicsSystem::SwapHandler(const SwapParameters& swap_params) {
  SCOPE_profile_cpu_f("gpu");

  // Swap requested. Synchronously post a request to the loop so that
  // we do the swap in the right thread.
  control_->SynchronousRepaint([&]() {
    glBlitNamedFramebuffer(swap_params.framebuffer, 0, swap_params.x,
                           swap_params.y, swap_params.x + swap_params.width,
                           swap_params.y + swap_params.height, 0, 0,
                           control_->width(), control_->height(),
                           GL_COLOR_BUFFER_BIT, GL_LINEAR);
  });
}

uint64_t GL4GraphicsSystem::ReadRegister(uint64_t addr) {
  uint32_t r = addr & 0xFFFF;
  if (FLAGS_trace_ring_buffer) {
    XELOGGPU("ReadRegister(%.4X)", r);
  }

  switch (r) {
    case 0x3C00:  // ?
      return 0x08100748;
    case 0x3C04:  // ?
      return 0x0000200E;
    case 0x6530:  // Scanline?
      return 0x000002D0;
    case 0x6544:  // ? vblank pending?
      return 1;
    case 0x6584:  // Screen res - 1280x720
      return 0x050002D0;
  }

  assert_true(r >= 0 && r < RegisterFile::kRegisterCount);
  return register_file_.values[r].u32;
}

void GL4GraphicsSystem::WriteRegister(uint64_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;
  if (FLAGS_trace_ring_buffer) {
    XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);
  }

  switch (r) {
    case 0x0714:  // CP_RB_WPTR
      command_processor_->UpdateWritePointer(static_cast<uint32_t>(value));
      break;
    case 0x6110:  // ? swap related?
      XELOGW("Unimplemented GPU register %.4X write: %.8X", r, value);
      return;
    default:
      XELOGW("Unknown GPU register %.4X write: %.8X", r, value);
      break;
  }

  assert_true(r >= 0 && r < RegisterFile::kRegisterCount);
  register_file_.values[r].u32 = static_cast<uint32_t>(value);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
