/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_graphics_system.h"

#include <cstring>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/gpu/gl4/gl4_gpu_flags.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/tracing.h"
#include "xenia/ui/gl/gl_profiler_display.h"

namespace xe {
namespace gpu {
namespace gl4 {

void InitializeIfNeeded();
void CleanupOnShutdown();

void InitializeIfNeeded() {
  static bool has_initialized = false;
  if (has_initialized) {
    return;
  }
  has_initialized = true;

  //

  atexit(CleanupOnShutdown);
}

void CleanupOnShutdown() {}

std::unique_ptr<GraphicsSystem> GL4GraphicsSystem::Create(Emulator* emulator) {
  InitializeIfNeeded();
  return std::make_unique<GL4GraphicsSystem>(emulator);
}

GL4GraphicsSystem::GL4GraphicsSystem(Emulator* emulator)
    : GraphicsSystem(emulator), worker_running_(false) {}

GL4GraphicsSystem::~GL4GraphicsSystem() = default;

X_STATUS GL4GraphicsSystem::Setup(cpu::Processor* processor,
                                  ui::PlatformLoop* target_loop,
                                  ui::PlatformWindow* target_window) {
  auto result = GraphicsSystem::Setup(processor, target_loop, target_window);
  if (result) {
    return result;
  }

  // Create rendering control.
  // This must happen on the UI thread.
  xe::threading::Fence control_ready_fence;
  std::unique_ptr<xe::ui::gl::GLContext> processor_context;
  target_loop_->Post([&]() {
    // Setup the GL control that actually does the drawing.
    // We run here in the loop and only touch it (and its context) on this
    // thread. That means some sync-fu when we want to swap.
    control_ = std::make_unique<xe::ui::gl::WGLControl>(target_loop_);
    target_window_->AddChild(control_.get());

    // Setup the GL context the command processor will do all its drawing in.
    // It's shared with the control context so that we can resolve framebuffers
    // from it.
    processor_context = control_->context()->CreateShared();

    {
      xe::ui::gl::GLContextLock context_lock(control_->context());
      auto profiler_display =
          std::make_unique<xe::ui::gl::GLProfilerDisplay>(control_.get());
      Profiler::set_display(std::move(profiler_display));
    }

    control_ready_fence.Signal();
  });
  control_ready_fence.Wait();

  // Create command processor. This will spin up a thread to process all
  // incoming ringbuffer packets.
  command_processor_ = std::make_unique<CommandProcessor>(this);
  if (!command_processor_->Initialize(std::move(processor_context))) {
    XELOGE("Unable to initialize command processor");
    return X_STATUS_UNSUCCESSFUL;
  }
  command_processor_->set_swap_handler(
      [this](const SwapParameters& swap_params) { SwapHandler(swap_params); });

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // 60hz vsync timer.
  worker_running_ = true;
  worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          emulator()->kernel_state(), 128 * 1024, 0, [this]() {
            uint64_t vsync_duration = FLAGS_vsync ? 16 : 1;
            uint64_t last_frame_time = Clock::QueryGuestTickCount();
            while (worker_running_) {
              uint64_t current_time = Clock::QueryGuestTickCount();
              uint64_t elapsed = (current_time - last_frame_time) /
                                 (Clock::guest_tick_frequency() / 1000);
              if (elapsed >= vsync_duration) {
                MarkVblank();
                last_frame_time = current_time;
              }
              Sleep(1);
            }
            return 0;
          }));
  worker_thread_->set_name("GL4 Vsync");
  worker_thread_->Create();

  if (FLAGS_trace_gpu_stream) {
    BeginTracing();
  }

  return X_STATUS_SUCCESS;
}

void GL4GraphicsSystem::Shutdown() {
  EndTracing();

  worker_running_ = false;
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();

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

void GL4GraphicsSystem::RequestSwap() {
  command_processor_->CallInThread([&]() { command_processor_->IssueSwap(); });
}

void GL4GraphicsSystem::RequestFrameTrace() {
  command_processor_->RequestFrameTrace(xe::to_wstring(FLAGS_trace_gpu_prefix));
}

void GL4GraphicsSystem::BeginTracing() {
  command_processor_->BeginTracing(xe::to_wstring(FLAGS_trace_gpu_prefix));
}

void GL4GraphicsSystem::EndTracing() { command_processor_->EndTracing(); }

void GL4GraphicsSystem::PlayTrace(const uint8_t* trace_data, size_t trace_size,
                                  TracePlaybackMode playback_mode) {
  command_processor_->CallInThread(
      [this, trace_data, trace_size, playback_mode]() {
        command_processor_->set_swap_mode(SwapMode::kIgnored);

        auto trace_ptr = trace_data;
        bool pending_break = false;
        const PacketStartCommand* pending_packet = nullptr;
        while (trace_ptr < trace_data + trace_size) {
          auto type =
              static_cast<TraceCommandType>(xe::load<uint32_t>(trace_ptr));
          switch (type) {
            case TraceCommandType::kPrimaryBufferStart: {
              auto cmd =
                  reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
              //
              trace_ptr += sizeof(*cmd) + cmd->count * 4;
              break;
            }
            case TraceCommandType::kPrimaryBufferEnd: {
              auto cmd =
                  reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
              //
              trace_ptr += sizeof(*cmd);
              break;
            }
            case TraceCommandType::kIndirectBufferStart: {
              auto cmd = reinterpret_cast<const IndirectBufferStartCommand*>(
                  trace_ptr);
              //
              trace_ptr += sizeof(*cmd) + cmd->count * 4;
              break;
            }
            case TraceCommandType::kIndirectBufferEnd: {
              auto cmd =
                  reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
              //
              trace_ptr += sizeof(*cmd);
              break;
            }
            case TraceCommandType::kPacketStart: {
              auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
              trace_ptr += sizeof(*cmd);
              std::memcpy(memory()->TranslatePhysical(cmd->base_ptr), trace_ptr,
                          cmd->count * 4);
              trace_ptr += cmd->count * 4;
              pending_packet = cmd;
              break;
            }
            case TraceCommandType::kPacketEnd: {
              auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
              trace_ptr += sizeof(*cmd);
              if (pending_packet) {
                command_processor_->ExecutePacket(pending_packet->base_ptr,
                                                  pending_packet->count);
                pending_packet = nullptr;
              }
              if (pending_break) {
                return;
              }
              break;
            }
            case TraceCommandType::kMemoryRead: {
              auto cmd = reinterpret_cast<const MemoryReadCommand*>(trace_ptr);
              trace_ptr += sizeof(*cmd);
              std::memcpy(memory()->TranslatePhysical(cmd->base_ptr), trace_ptr,
                          cmd->length);
              trace_ptr += cmd->length;
              break;
            }
            case TraceCommandType::kMemoryWrite: {
              auto cmd = reinterpret_cast<const MemoryWriteCommand*>(trace_ptr);
              trace_ptr += sizeof(*cmd);
              // ?
              trace_ptr += cmd->length;
              break;
            }
            case TraceCommandType::kEvent: {
              auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
              trace_ptr += sizeof(*cmd);
              switch (cmd->event_type) {
                case EventType::kSwap: {
                  if (playback_mode == TracePlaybackMode::kBreakOnSwap) {
                    pending_break = true;
                  }
                  break;
                }
              }
              break;
            }
          }
        }

        command_processor_->set_swap_mode(SwapMode::kNormal);
      });
}

void GL4GraphicsSystem::ClearCaches() {
  command_processor_->CallInThread(
      [&]() { command_processor_->ClearCaches(); });
}

void GL4GraphicsSystem::MarkVblank() {
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
  control_->SynchronousRepaint([this, swap_params]() {
    if (!swap_params.framebuffer_texture) {
      // no-op.
      return;
    }
    Rect2D src_rect(swap_params.x, swap_params.y, swap_params.width,
                    swap_params.height);
    Rect2D dest_rect(0, 0, control_->width(), control_->height());
    control_->context()->blitter()->BlitTexture2D(
        swap_params.framebuffer_texture, src_rect, dest_rect, GL_LINEAR);
  });
}

uint64_t GL4GraphicsSystem::ReadRegister(uint32_t addr) {
  uint32_t r = addr & 0xFFFF;

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

void GL4GraphicsSystem::WriteRegister(uint32_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;

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
