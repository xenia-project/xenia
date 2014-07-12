/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/graphics_system.h>

#include <xenia/emulator.h>
#include <xenia/cpu/processor.h>
#include <xenia/gpu/command_processor.h>
#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/register_file.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


GraphicsSystem::GraphicsSystem(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    thread_(nullptr), running_(false), driver_(nullptr),
    command_processor_(nullptr),
    interrupt_callback_(0), interrupt_callback_data_(0),
    last_interrupt_time_(0), thread_wait_(nullptr) {
  // Create the run loop used for any windows/etc.
  // This must be done on the thread we create the driver.
  run_loop_ = xe_run_loop_create();
  thread_wait_ = CreateEvent(NULL, TRUE, FALSE, NULL);
}

GraphicsSystem::~GraphicsSystem() {
  CloseHandle(thread_wait_);
}

X_STATUS GraphicsSystem::Setup() {
  processor_ = emulator_->processor();

  // Create worker.
  command_processor_ = new CommandProcessor(this, memory_);

  // Let the processor know we want register access callbacks.
  emulator_->memory()->AddMappedRange(
      0x7FC80000,
      0xFFFF0000,
      0x0000FFFF,
      this,
      reinterpret_cast<MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // Create worker thread.
  // This will initialize the graphics system.
  // Init needs to happen there so that any thread-local stuff
  // is created on the right thread.
  running_ = true;
  thread_ = xe_thread_create(
      "GraphicsSystem",
      (xe_thread_callback)ThreadStartThunk, this);
  xe_thread_start(thread_);
  WaitForSingleObject(thread_wait_, INFINITE);
  return X_STATUS_SUCCESS;
}

void GraphicsSystem::ThreadStart() {
  xe_run_loop_ref run_loop = xe_run_loop_retain(run_loop_);

  // Initialize driver and ringbuffer.
  Initialize();
  assert_not_null(driver_);
  SetEvent(thread_wait_);

  // Main run loop.
  while (running_) {
    // Peek main run loop.
    {
      SCOPE_profile_cpu_i("gpu", "GraphicsSystemRunLoopPump");
      if (xe_run_loop_pump(run_loop)) {
        break;
      }
    }
    if (!running_) {
      break;
    }

    // Pump worker.
    command_processor_->Pump();

    if (!running_) {
      break;
    }

    // Pump graphics system.
    Pump();
  }
  running_ = false;

  xe_run_loop_release(run_loop);
}

void GraphicsSystem::Initialize() {
}

void GraphicsSystem::Shutdown() {
  running_ = false;
  xe_thread_join(thread_);
  xe_thread_release(thread_);

  delete command_processor_;

  xe_run_loop_release(run_loop_);
}

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback(%.4X, %.4X)", callback, user_data);
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  // TODO(benvanik): an event?
  while (!driver_) {
    Sleep(0);
  }
  assert_not_null(driver_);
  command_processor_->Initialize(driver_, ptr, page_count);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size) {
  command_processor_->EnableReadPointerWriteBack(ptr, block_size);
}

uint64_t GraphicsSystem::ReadRegister(uint64_t addr) {
  uint32_t r = addr & 0xFFFF;
  if (FLAGS_trace_ring_buffer) {
    XELOGGPU("ReadRegister(%.4X)", r);
  }

  RegisterFile* regs = driver_->register_file();

  switch (r) {
  case 0x6530: // ????
    return 1;
  case 0x6544: // ? vblank pending?
    return 1;
  case 0x6584: // ????
    return 1;
  }

  assert_true(r >= 0 && r < RegisterFile::kRegisterCount);
  return regs->values[r].u32;
}

void GraphicsSystem::WriteRegister(uint64_t addr, uint64_t value) {
  uint32_t r = addr & 0xFFFF;
  if (FLAGS_trace_ring_buffer) {
    XELOGGPU("WriteRegister(%.4X, %.8X)", r, value);
  }

  RegisterFile* regs = driver_->register_file();

  switch (r) {
    case 0x0714: // CP_RB_WPTR
      command_processor_->UpdateWritePointer((uint32_t)value);
      break;
    default:
      XELOGW("Unknown GPU register %.4X write: %.8X", r, value);
      break;
  }

  assert_true(r >= 0 && r < RegisterFile::kRegisterCount);
  regs->values[r].u32 = (uint32_t)value;
}

void GraphicsSystem::MarkVblank() {
  command_processor_->increment_counter();
}

void GraphicsSystem::DispatchInterruptCallback(
    uint32_t source, uint32_t cpu) {
  // Pick a CPU, if needed. We're going to guess 2. Because.
  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }

  // NOTE: we may be executing in some random thread.
  last_interrupt_time_ = xe_pal_now();
  if (!interrupt_callback_) {
    return;
  }
  uint64_t args[] = { source, interrupt_callback_data_ };
  processor_->ExecuteInterrupt(
      cpu, interrupt_callback_, args, XECOUNT(args));
}
