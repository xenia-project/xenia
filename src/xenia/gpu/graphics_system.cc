/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/graphics_system.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace gpu {

// Nvidia Optimus/AMD PowerXpress support.
// These exports force the process to trigger the discrete GPU in multi-GPU
// systems.
// http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// http://stackoverflow.com/questions/17458803/amd-equivalent-to-nvoptimusenablement
#if XE_PLATFORM_WIN32
extern "C" {
__declspec(dllexport) uint32_t NvOptimusEnablement = 0x00000001;
__declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}  // extern "C"
#endif  // XE_PLATFORM_WIN32

GraphicsSystem::GraphicsSystem() : vsync_worker_running_(false) {}

GraphicsSystem::~GraphicsSystem() = default;

X_STATUS GraphicsSystem::Setup(cpu::Processor* processor,
                               kernel::KernelState* kernel_state,
                               ui::Window* target_window) {
  memory_ = processor->memory();
  processor_ = processor;
  kernel_state_ = kernel_state;
  target_window_ = target_window;

  // Initialize display and rendering context.
  // This must happen on the UI thread.
  std::unique_ptr<xe::ui::GraphicsContext> processor_context = nullptr;
  if (provider_) {
    if (target_window_) {
      target_window_->loop()->PostSynchronous([&]() {
        // Create the context used for presentation.
        assert_null(target_window->context());
        target_window_->set_context(provider_->CreateContext(target_window_));

        // Setup the context the command processor will do all its drawing in.
        // It's shared with the display context so that we can resolve
        // framebuffers from it.
        processor_context = provider()->CreateOffscreenContext();
      });
    } else {
      processor_context = provider()->CreateOffscreenContext();
    }

    if (!processor_context) {
      xe::FatalError(
          "Unable to initialize graphics context. Xenia requires OpenGL 4.5 or "
          "Vulkan support. Ensure you have the latest drivers for your GPU and "
          "that it supports OpenGL or Vulkan. See http://xenia.jp/faq/ for "
          "more information.");
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  // Create command processor. This will spin up a thread to process all
  // incoming ringbuffer packets.
  command_processor_ = CreateCommandProcessor();
  if (!command_processor_->Initialize(std::move(processor_context))) {
    XELOGE("Unable to initialize command processor");
    return X_STATUS_UNSUCCESSFUL;
  }

  if (target_window) {
    command_processor_->set_swap_request_handler(
        [this]() { target_window_->Invalidate(); });

    // Watch for paint requests to do our swap.
    target_window->on_painting.AddListener(
        [this](xe::ui::UIEvent* e) { Swap(e); });

    // Watch for context lost events.
    target_window->on_context_lost.AddListener(
        [this](xe::ui::UIEvent* e) { Reset(); });
  } else {
    command_processor_->set_swap_request_handler([]() {});
  }

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(ReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(WriteRegisterThunk));

  // 60hz vsync timer.
  vsync_worker_running_ = true;
  vsync_worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        uint64_t vsync_duration = FLAGS_vsync ? 16 : 1;
        uint64_t last_frame_time = Clock::QueryGuestTickCount();
        while (vsync_worker_running_) {
          uint64_t current_time = Clock::QueryGuestTickCount();
          uint64_t elapsed = (current_time - last_frame_time) /
                             (Clock::guest_tick_frequency() / 1000);
          if (elapsed >= vsync_duration) {
            MarkVblank();
            last_frame_time = current_time;
          }
          xe::threading::Sleep(std::chrono::milliseconds(1));
        }
        return 0;
      }));
  // As we run vblank interrupts the debugger must be able to suspend us.
  vsync_worker_thread_->set_can_debugger_suspend(true);
  vsync_worker_thread_->set_name("GraphicsSystem Vsync");
  vsync_worker_thread_->Create();

  if (FLAGS_trace_gpu_stream) {
    BeginTracing();
  }

  return X_STATUS_SUCCESS;
}

void GraphicsSystem::Shutdown() {
  if (command_processor_) {
    EndTracing();
  }

  if (command_processor_) {
    command_processor_->Shutdown();
    // TODO(benvanik): remove mapped range.
    command_processor_.reset();
  }

  if (vsync_worker_thread_) {
    vsync_worker_running_ = false;
    vsync_worker_thread_->Wait(0, 0, 0, nullptr);
    vsync_worker_thread_.reset();
  }
}

void GraphicsSystem::Reset() {
  // TODO(DrChat): Reset the system.
  XELOGI("Context lost; Reset invoked");
  Shutdown();

  xe::FatalError("Graphics device lost (probably due to an internal error)");
}

uint32_t GraphicsSystem::ReadRegisterThunk(void* ppc_context,
                                           GraphicsSystem* gs, uint32_t addr) {
  return gs->ReadRegister(addr);
}

void GraphicsSystem::WriteRegisterThunk(void* ppc_context, GraphicsSystem* gs,
                                        uint32_t addr, uint32_t value) {
  gs->WriteRegister(addr, value);
}

uint32_t GraphicsSystem::ReadRegister(uint32_t addr) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case 0x0F00:  // ?
      return 0x08100748;
    case 0x0F01:  // RB_BC_CONTROL
      return 0x0000200E;
    case 0x194C:  // R500_D1MODE_V_COUNTER(?) / scanline(?)
      return 0x000002D0;
    case 0x1951:  // ? vblank pending?
      return 1;
    case 0x1961:  // AVIVO_D1MODE_VIEWPORT_SIZE
                  // Screen res - 1280x720
                  // [width(0x0FFF), height(0x0FFF)]
      return 0x050002D0;
    default:
      if (!register_file_.GetRegisterInfo(r)) {
        XELOGE("GPU: Read from unknown register (%.4X)", r);
      }
  }

  assert_true(r < RegisterFile::kRegisterCount);
  return register_file_.values[r].u32;
}

void GraphicsSystem::WriteRegister(uint32_t addr, uint32_t value) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case 0x01C5:  // CP_RB_WPTR
      command_processor_->UpdateWritePointer(value);
      break;
    case 0x1844:  // AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS
      break;
    default:
      XELOGW("Unknown GPU register %.4X write: %.8X", r, value);
      break;
  }

  assert_true(r < RegisterFile::kRegisterCount);
  register_file_.values[r].u32 = value;
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t log2_size) {
  command_processor_->InitializeRingBuffer(ptr, (log2_size | 0x2) + 1);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size) {
  command_processor_->EnableReadPointerWriteBack(ptr, block_size);
}

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

  // XELOGGPU("Dispatching GPU interrupt at %.8X w/ mode %d on cpu %d",
  //          interrupt_callback_, source, cpu);

  uint64_t args[] = {source, interrupt_callback_data_};
  processor_->ExecuteInterrupt(thread->thread_state(), interrupt_callback_,
                               args, xe::countof(args));
}

void GraphicsSystem::MarkVblank() {
  SCOPE_profile_cpu_f("gpu");

  // Increment vblank counter (so the game sees us making progress).
  command_processor_->increment_counter();

  // TODO(benvanik): we shouldn't need to do the dispatch here, but there's
  //     something wrong and the CP will block waiting for code that
  //     needs to be run in the interrupt.
  DispatchInterruptCallback(0, 2);
}

void GraphicsSystem::ClearCaches() {
  command_processor_->CallInThread(
      [&]() { command_processor_->ClearCaches(); });
}

void GraphicsSystem::RequestFrameTrace() {
  command_processor_->RequestFrameTrace(xe::to_wstring(FLAGS_trace_gpu_prefix));
}

void GraphicsSystem::BeginTracing() {
  command_processor_->BeginTracing(xe::to_wstring(FLAGS_trace_gpu_prefix));
}

void GraphicsSystem::EndTracing() { command_processor_->EndTracing(); }

void GraphicsSystem::Pause() {
  paused_ = true;

  command_processor_->Pause();
}

void GraphicsSystem::Resume() {
  paused_ = false;

  command_processor_->Resume();
}

bool GraphicsSystem::Save(ByteStream* stream) {
  stream->Write<uint32_t>(interrupt_callback_);
  stream->Write<uint32_t>(interrupt_callback_data_);

  return command_processor_->Save(stream);
}

bool GraphicsSystem::Restore(ByteStream* stream) {
  interrupt_callback_ = stream->Read<uint32_t>();
  interrupt_callback_data_ = stream->Read<uint32_t>();

  return command_processor_->Restore(stream);
}

}  // namespace gpu
}  // namespace xe
