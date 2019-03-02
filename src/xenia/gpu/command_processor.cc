/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/command_processor.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

CommandProcessor::CommandProcessor(GraphicsSystem* graphics_system,
                                   kernel::KernelState* kernel_state)
    : memory_(graphics_system->memory()),
      kernel_state_(kernel_state),
      graphics_system_(graphics_system),
      register_file_(graphics_system_->register_file()),
      trace_writer_(graphics_system->memory()->physical_membase()),
      worker_running_(true),
      write_ptr_index_event_(xe::threading::Event::CreateAutoResetEvent(false)),
      write_ptr_index_(0) {}

CommandProcessor::~CommandProcessor() = default;

bool CommandProcessor::Initialize(
    std::unique_ptr<xe::ui::GraphicsContext> context) {
  context_ = std::move(context);

  // Initialize the gamma ramps to their default (linear) values - taken from
  // what games set when starting.
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i * 1023 / 255;
    gamma_ramp_.normal[i].value = value | (value << 10) | (value << 20);
  }
  for (uint32_t i = 0; i < 128; ++i) {
    uint32_t value = (i * 65535 / 127) & ~63;
    if (i < 127) {
      value |= 0x200 << 16;
    }
    for (uint32_t j = 0; j < 3; ++j) {
      gamma_ramp_.pwl[i].values[j].value = value;
    }
  }
  dirty_gamma_ramp_normal_ = true;
  dirty_gamma_ramp_pwl_ = true;

  worker_running_ = true;
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->set_name("GraphicsSystem Command Processor");
  worker_thread_->Create();

  return true;
}

void CommandProcessor::Shutdown() {
  EndTracing();

  worker_running_ = false;
  write_ptr_index_event_->Set();
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();
}

void CommandProcessor::RequestFrameTrace(const std::wstring& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming trace; cannot also trace frame.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace already pending; ignoring.");
    return;
  }
  trace_state_ = TraceState::kSingleFrame;
  trace_frame_path_ = root_path;
}

void CommandProcessor::BeginTracing(const std::wstring& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming already active; ignoring request.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace pending; ignoring streaming request.");
    return;
  }
  // Streaming starts on the next primary buffer execute.
  trace_state_ = TraceState::kStreaming;
  trace_stream_path_ = root_path;
}

void CommandProcessor::EndTracing() {
  if (!trace_writer_.is_open()) {
    return;
  }
  assert_true(trace_state_ == TraceState::kStreaming);
  trace_writer_.Close();
}

void CommandProcessor::CallInThread(std::function<void()> fn) {
  if (pending_fns_.empty() &&
      kernel::XThread::IsInThread(worker_thread_.get())) {
    fn();
  } else {
    pending_fns_.push(std::move(fn));
  }
}

void CommandProcessor::ClearCaches() {}

void CommandProcessor::WorkerThreadMain() {
  context_->MakeCurrent();
  if (!SetupContext()) {
    xe::FatalError("Unable to setup command processor internal state");
    return;
  }

  while (worker_running_) {
    while (!pending_fns_.empty()) {
      auto fn = std::move(pending_fns_.front());
      pending_fns_.pop();
      fn();
    }

    uint32_t write_ptr_index = write_ptr_index_.load();
    if (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index) {
      SCOPE_profile_cpu_i("gpu", "xe::gpu::CommandProcessor::Stall");
      // We've run out of commands to execute.
      // We spin here waiting for new ones, as the overhead of waiting on our
      // event is too high.
      PrepareForWait();
      uint32_t loop_count = 0;
      do {
        // If we spin around too much, revert to a "low-power" state.
        if (loop_count > 500) {
          const int wait_time_ms = 5;
          xe::threading::Wait(write_ptr_index_event_.get(), true,
                              std::chrono::milliseconds(wait_time_ms));
        }

        xe::threading::MaybeYield();
        loop_count++;
        write_ptr_index = write_ptr_index_.load();
      } while (worker_running_ && pending_fns_.empty() &&
               (write_ptr_index == 0xBAADF00D ||
                read_ptr_index_ == write_ptr_index));
      ReturnFromWait();
      if (!worker_running_ || !pending_fns_.empty()) {
        continue;
      }
    }
    assert_true(read_ptr_index_ != write_ptr_index);

    // Execute. Note that we handle wraparound transparently.
    read_ptr_index_ = ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);

    // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      xe::store_and_swap<uint32_t>(
          memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);
    }

    // FIXME: We're supposed to process the WAIT_UNTIL register at this point,
    // but no games seem to actually use it.
  }

  ShutdownContext();
}

void CommandProcessor::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  threading::Fence fence;
  CallInThread([&fence]() {
    fence.Signal();
    threading::Thread::GetCurrentThread()->Suspend();
  });

  // HACK - Prevents a hang in IssueSwap()
  swap_state_.pending = false;

  fence.Wait();
}

void CommandProcessor::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  worker_thread_->thread()->Resume();
}

bool CommandProcessor::Save(ByteStream* stream) {
  assert_true(paused_);

  stream->Write<uint32_t>(primary_buffer_ptr_);
  stream->Write<uint32_t>(primary_buffer_size_);
  stream->Write<uint32_t>(read_ptr_index_);
  stream->Write<uint32_t>(read_ptr_update_freq_);
  stream->Write<uint32_t>(read_ptr_writeback_ptr_);
  stream->Write<uint32_t>(write_ptr_index_.load());

  return true;
}

bool CommandProcessor::Restore(ByteStream* stream) {
  assert_true(paused_);

  primary_buffer_ptr_ = stream->Read<uint32_t>();
  primary_buffer_size_ = stream->Read<uint32_t>();
  read_ptr_index_ = stream->Read<uint32_t>();
  read_ptr_update_freq_ = stream->Read<uint32_t>();
  read_ptr_writeback_ptr_ = stream->Read<uint32_t>();
  write_ptr_index_.store(stream->Read<uint32_t>());

  return true;
}

bool CommandProcessor::SetupContext() { return true; }

void CommandProcessor::ShutdownContext() { context_.reset(); }

void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t log2_size) {
  read_ptr_index_ = 0;
  primary_buffer_ptr_ = ptr;
  primary_buffer_size_ = 1 << log2_size;
}

void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, number of quadwords read between updates of the
  //              read pointer.
  read_ptr_update_freq_ =
      static_cast<uint32_t>(pow(2.0, static_cast<double>(block_size)) / 4);
}

void CommandProcessor::UpdateWritePointer(uint32_t value) {
  write_ptr_index_ = value;
  write_ptr_index_event_->Set();
}

void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  RegisterFile* regs = register_file_;
  if (index >= RegisterFile::kRegisterCount) {
    XELOGW("CommandProcessor::WriteRegister index out of bounds: %d", index);
    return;
  }

  regs->values[index].u32 = value;
  if (!regs->GetRegisterInfo(index)) {
    XELOGW("GPU: Write to unknown register (%.4X = %.8X)", index, value);
  }

  // If this is a COHER register, set the dirty flag.
  // This will block the command processor the next time it WAIT_MEM_REGs and
  // allow us to synchronize the memory.
  if (index == XE_GPU_REG_COHER_STATUS_HOST) {
    regs->values[index].u32 |= 0x80000000ul;
  }

  // Scratch register writeback.
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7) {
    uint32_t scratch_reg = index - XE_GPU_REG_SCRATCH_REG0;
    if ((1 << scratch_reg) & regs->values[XE_GPU_REG_SCRATCH_UMSK].u32) {
      // Enabled - write to address.
      uint32_t scratch_addr = regs->values[XE_GPU_REG_SCRATCH_ADDR].u32;
      uint32_t mem_addr = scratch_addr + (scratch_reg * 4);
      xe::store_and_swap<uint32_t>(memory_->TranslatePhysical(mem_addr), value);
    }
  }
}

void CommandProcessor::UpdateGammaRampValue(GammaRampType type,
                                            uint32_t value) {
  RegisterFile* regs = register_file_;

  auto index = regs->values[XE_GPU_REG_DC_LUT_RW_INDEX].u32;

  auto mask = regs->values[XE_GPU_REG_DC_LUT_WRITE_EN_MASK].u32;
  auto mask_lo = (mask >> 0) & 0x7;
  auto mask_hi = (mask >> 3) & 0x7;

  // If games update individual components we're going to have a problem.
  assert_true(mask_lo == 0 || mask_lo == 7);
  assert_true(mask_hi == 0);

  if (mask_lo) {
    switch (type) {
      case GammaRampType::kNormal:
        assert_true(regs->values[XE_GPU_REG_DC_LUT_RW_MODE].u32 == 0);
        gamma_ramp_.normal[index].value = value;
        dirty_gamma_ramp_normal_ = true;
        break;
      case GammaRampType::kPWL:
        assert_true(regs->values[XE_GPU_REG_DC_LUT_RW_MODE].u32 == 1);
        gamma_ramp_.pwl[index].values[gamma_ramp_rw_subindex_].value = value;
        gamma_ramp_rw_subindex_ = (gamma_ramp_rw_subindex_ + 1) % 3;
        dirty_gamma_ramp_pwl_ = true;
        break;
      default:
        assert_unhandled_case(type);
    }
  }
}

void CommandProcessor::MakeCoherent() {
  SCOPE_profile_cpu_f("gpu");

  // Status host often has 0x01000000 or 0x03000000.
  // This is likely toggling VC (vertex cache) or TC (texture cache).
  // Or, it also has a direction in here maybe - there is probably
  // some way to check for dest coherency (what all the COHER_DEST_BASE_*
  // registers are for).
  // Best docs I've found on this are here:
  // https://web.archive.org/web/20160711162346/https://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2013/10/R6xx_R7xx_3D.pdf
  // https://cgit.freedesktop.org/xorg/driver/xf86-video-radeonhd/tree/src/r6xx_accel.c?id=3f8b6eccd9dba116cc4801e7f80ce21a879c67d2#n454

  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;
  auto base_host = regs->values[XE_GPU_REG_COHER_BASE_HOST].u32;
  auto size_host = regs->values[XE_GPU_REG_COHER_SIZE_HOST].u32;

  if (!(status_host & 0x80000000ul)) {
    return;
  }

  const char* action = "N/A";
  if ((status_host & 0x03000000) == 0x03000000) {
    action = "VC | TC";
  } else if (status_host & 0x02000000) {
    action = "TC";
  } else if (status_host & 0x01000000) {
    action = "VC";
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  XELOGD("Make %.8X -> %.8X (%db) coherent, action = %s", base_host,
         base_host + size_host, size_host, action);

  // Mark coherent.
  status_host &= ~0x80000000ul;
  regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32 = status_host;
}

void CommandProcessor::PrepareForWait() { trace_writer_.Flush(); }

void CommandProcessor::ReturnFromWait() {}

void CommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                 uint32_t frontbuffer_width,
                                 uint32_t frontbuffer_height) {
  SCOPE_profile_cpu_f("gpu");
  if (!swap_request_handler_) {
    return;
  }

  // If there was a swap pending we drop it on the floor.
  // This prevents the display from pulling the backbuffer out from under us.
  // If we skip a lot then we may need to buffer more, but as the display
  // thread should be fairly idle that shouldn't happen.
  if (!FLAGS_vsync) {
    std::lock_guard<std::mutex> lock(swap_state_.mutex);
    if (swap_state_.pending) {
      swap_state_.pending = false;
      // TODO(benvanik): frame skip counter.
      XELOGW("Skipped frame!");
    }
  } else {
    // Spin until no more pending swap.
    while (worker_running_) {
      {
        std::lock_guard<std::mutex> lock(swap_state_.mutex);
        if (!swap_state_.pending) {
          break;
        }
      }
      xe::threading::MaybeYield();
    }
  }

  PerformSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);

  {
    // Set pending so that the display will swap the next time it can.
    std::lock_guard<std::mutex> lock(swap_state_.mutex);
    swap_state_.pending = true;
  }

  // Notify the display a swap is pending so that our changes are picked up.
  // It does the actual front/back buffer swap.
  swap_request_handler_();
}

uint32_t CommandProcessor::ExecutePrimaryBuffer(uint32_t read_index,
                                                uint32_t write_index) {
  SCOPE_profile_cpu_f("gpu");

  // If we have a pending trace stream open it now. That way we ensure we get
  // all commands.
  if (!trace_writer_.is_open() && trace_state_ == TraceState::kStreaming) {
    uint32_t title_id = kernel_state_->GetExecutableModule()
                            ? kernel_state_->GetExecutableModule()->title_id()
                            : 0;
    auto file_name = xe::format_string(L"%8X_stream.xtr", title_id);
    auto path = trace_stream_path_ + file_name;
    trace_writer_.Open(path, title_id);
  }

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + read_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + write_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, write_index - read_index);

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(primary_buffer_ptr_),
                    primary_buffer_size_);
  reader.set_read_offset(read_index * sizeof(uint32_t));
  reader.set_write_offset(write_index * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // This probably should be fatal - but we're going to continue anyways.
      XELOGE("**** PRIMARY RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  trace_writer_.WritePrimaryBufferEnd();

  return write_index;
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.WriteIndirectBufferStart(ptr, count * sizeof(uint32_t));

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // Return up a level if we encounter a bad packet.
      XELOGE("**** INDIRECT RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  trace_writer_.WriteIndirectBufferEnd();
}

void CommandProcessor::ExecutePacket(uint32_t ptr, uint32_t count) {
  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      XELOGE("**** ExecutePacket: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());
}

bool CommandProcessor::ExecutePacket(RingBuffer* reader) {
  const uint32_t packet = reader->ReadAndSwap<uint32_t>();
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
    trace_writer_.WritePacketEnd();
    return true;
  }

  switch (packet_type) {
    case 0x00:
      return ExecutePacketType0(reader, packet);
    case 0x01:
      return ExecutePacketType1(reader, packet);
    case 0x02:
      return ExecutePacketType2(reader, packet);
    case 0x03:
      return ExecutePacketType3(reader, packet);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

bool CommandProcessor::ExecutePacketType0(RingBuffer* reader, uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE("ExecutePacketType0 overflow (read count %.8X, packet count %.8X)",
           reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  for (uint32_t m = 0; m < count; m++) {
    uint32_t reg_data = reader->ReadAndSwap<uint32_t>();
    uint32_t target_index = write_one_reg ? base_index : base_index + m;
    WriteRegister(target_index, reg_data);
  }

  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingBuffer* reader, uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader->ReadAndSwap<uint32_t>();
  uint32_t reg_data_2 = reader->ReadAndSwap<uint32_t>();
  WriteRegister(reg_index_1, reg_data_1);
  WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingBuffer* reader, uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingBuffer* reader, uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->read_offset();

  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE("ExecutePacketType3 overflow (read count %.8X, packet count %.8X)",
           reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  // To handle nesting behavior when tracing we special case indirect buffers.
  if (opcode == PM4_INDIRECT_BUFFER) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 2);
  } else {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);
  }

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  // We also skip predicated swaps, as they are never valid (probably?).
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    if (!any_pass || opcode == PM4_XE_SWAP) {
      reader->AdvanceRead(count * sizeof(uint32_t));
      trace_writer_.WritePacketEnd();
      return true;
    }
  }

  bool result = false;
  switch (opcode) {
    case PM4_ME_INIT:
      result = ExecutePacketType3_ME_INIT(reader, packet, count);
      break;
    case PM4_NOP:
      result = ExecutePacketType3_NOP(reader, packet, count);
      break;
    case PM4_INTERRUPT:
      result = ExecutePacketType3_INTERRUPT(reader, packet, count);
      break;
    case PM4_XE_SWAP:
      result = ExecutePacketType3_XE_SWAP(reader, packet, count);
      break;
    case PM4_INDIRECT_BUFFER:
    case PM4_INDIRECT_BUFFER_PFD:
      result = ExecutePacketType3_INDIRECT_BUFFER(reader, packet, count);
      break;
    case PM4_WAIT_REG_MEM:
      result = ExecutePacketType3_WAIT_REG_MEM(reader, packet, count);
      break;
    case PM4_REG_RMW:
      result = ExecutePacketType3_REG_RMW(reader, packet, count);
      break;
    case PM4_REG_TO_MEM:
      result = ExecutePacketType3_REG_TO_MEM(reader, packet, count);
      break;
    case PM4_MEM_WRITE:
      result = ExecutePacketType3_MEM_WRITE(reader, packet, count);
      break;
    case PM4_COND_WRITE:
      result = ExecutePacketType3_COND_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE:
      result = ExecutePacketType3_EVENT_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_SHD:
      result = ExecutePacketType3_EVENT_WRITE_SHD(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_EXT:
      result = ExecutePacketType3_EVENT_WRITE_EXT(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_ZPD:
      result = ExecutePacketType3_EVENT_WRITE_ZPD(reader, packet, count);
      break;
    case PM4_DRAW_INDX:
      result = ExecutePacketType3_DRAW_INDX(reader, packet, count);
      break;
    case PM4_DRAW_INDX_2:
      result = ExecutePacketType3_DRAW_INDX_2(reader, packet, count);
      break;
    case PM4_SET_CONSTANT:
      result = ExecutePacketType3_SET_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_CONSTANT2:
      result = ExecutePacketType3_SET_CONSTANT2(reader, packet, count);
      break;
    case PM4_LOAD_ALU_CONSTANT:
      result = ExecutePacketType3_LOAD_ALU_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_SHADER_CONSTANTS:
      result = ExecutePacketType3_SET_SHADER_CONSTANTS(reader, packet, count);
      break;
    case PM4_IM_LOAD:
      result = ExecutePacketType3_IM_LOAD(reader, packet, count);
      break;
    case PM4_IM_LOAD_IMMEDIATE:
      result = ExecutePacketType3_IM_LOAD_IMMEDIATE(reader, packet, count);
      break;
    case PM4_INVALIDATE_STATE:
      result = ExecutePacketType3_INVALIDATE_STATE(reader, packet, count);
      break;
    case PM4_VIZ_QUERY:
      result = ExecutePacketType3_VIZ_QUERY(reader, packet, count);
      break;

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_MASK: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (val_hi << 32) | val_lo;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (val_hi << 32) | val_lo;
      result = true;
    } break;
    case PM4_CONTEXT_UPDATE: {
      assert_true(count == 1);
      uint64_t value = reader->ReadAndSwap<uint32_t>();
      XELOGGPU("GPU context update = %.8X", value);
      assert_true(value == 0);
      result = true;
      break;
    }

    default:
      XELOGGPU("Unimplemented GPU OPCODE: 0x%.2X\t\tCOUNT: %d\n", opcode,
               count);
      assert_always();
      reader->AdvanceRead(count * sizeof(uint32_t));
      break;
  }

  trace_writer_.WritePacketEnd();
  if (opcode == PM4_XE_SWAP) {
    // End the trace writer frame.
    if (trace_writer_.is_open()) {
      trace_writer_.WriteEvent(EventCommand::Type::kSwap);
      trace_writer_.Flush();
      if (trace_state_ == TraceState::kSingleFrame) {
        trace_state_ = TraceState::kDisabled;
        trace_writer_.Close();
      }
    } else if (trace_state_ == TraceState::kSingleFrame) {
      // New trace request - we only start tracing at the beginning of a frame.
      uint32_t title_id = kernel_state_->GetExecutableModule()->title_id();
      auto file_name = xe::format_string(L"%8X_%u.xtr", title_id, counter_ - 1);
      auto path = trace_frame_path_ + file_name;
      trace_writer_.Open(path, title_id);
    }
  }

  assert_true(reader->read_offset() ==
              (data_start_offset + (count * sizeof(uint32_t))) %
                  reader->capacity());
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  me_bin_.clear();
  for (uint32_t i = 0; i < count; i++) {
    me_bin_.push_back(reader->ReadAndSwap<uint32_t>());
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingBuffer* reader,
                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader->AdvanceRead(count * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader->ReadAndSwap<uint32_t>();
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  XELOGI("XE_SWAP");

  Profiler::Flip();

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t magic = reader->ReadAndSwap<uint32_t>();
  assert_true(magic == 'SWAP');

  // TODO(benvanik): only swap frontbuffer ptr.
  uint32_t frontbuffer_ptr = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_width = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_height = reader->ReadAndSwap<uint32_t>();
  reader->AdvanceRead((count - 4) * sizeof(uint32_t));

  if (swap_mode_ == SwapMode::kNormal) {
    IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  }

  ++counter_;
  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // indirect buffer dispatch
  uint32_t list_ptr = CpuToGpu(reader->ReadAndSwap<uint32_t>());
  uint32_t list_length = reader->ReadAndSwap<uint32_t>();
  assert_zero(list_length & ~0xFFFFF);
  list_length &= 0xFFFFF;
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value
  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t wait = reader->ReadAndSwap<uint32_t>();
  bool matched = false;
  do {
    uint32_t value;
    if (wait_info & 0x10) {
      // Memory.
      auto endianness = static_cast<Endian>(poll_reg_addr & 0x3);
      poll_reg_addr &= ~0x3;
      value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
      value = GpuSwap(value, endianness);
      trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    } else {
      // Register.
      assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
      value = register_file_->values[poll_reg_addr].u32;
      if (poll_reg_addr == XE_GPU_REG_COHER_STATUS_HOST) {
        MakeCoherent();
        value = register_file_->values[poll_reg_addr].u32;
      }
    }
    switch (wait_info & 0x7) {
      case 0x0:  // Never.
        matched = false;
        break;
      case 0x1:  // Less than reference.
        matched = (value & mask) < ref;
        break;
      case 0x2:  // Less than or equal to reference.
        matched = (value & mask) <= ref;
        break;
      case 0x3:  // Equal to reference.
        matched = (value & mask) == ref;
        break;
      case 0x4:  // Not equal to reference.
        matched = (value & mask) != ref;
        break;
      case 0x5:  // Greater than or equal to reference.
        matched = (value & mask) >= ref;
        break;
      case 0x6:  // Greater than reference.
        matched = (value & mask) > ref;
        break;
      case 0x7:  // Always
        matched = true;
        break;
    }
    if (!matched) {
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!FLAGS_vsync) {
          // User wants it fast and dangerous.
          xe::threading::MaybeYield();
        } else {
          xe::threading::Sleep(std::chrono::milliseconds(wait / 0x100));
        }
        xe::threading::SyncMemory();
        ReturnFromWait();

        if (!worker_running_) {
          // Short-circuited exit.
          return false;
        }
      } else {
        xe::threading::MaybeYield();
      }
    }
  } while (!matched);

  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader->ReadAndSwap<uint32_t>();
  uint32_t and_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t or_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF].u32;
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF].u32;
  } else {
    // & imm
    value &= and_mask;
  }
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF].u32;
  } else {
    // | imm
    value |= or_mask;
  }
  WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_TO_MEM(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // Copy Register to Memory (?)
  // Count is 2, assuming a Register Addr and a Memory Addr.

  uint32_t reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t mem_addr = reader->ReadAndSwap<uint32_t>();

  uint32_t reg_val;

  assert_true(reg_addr < RegisterFile::kRegisterCount);
  reg_val = register_file_->values[reg_addr].u32;

  auto endianness = static_cast<Endian>(mem_addr & 0x3);
  mem_addr &= ~0x3;
  reg_val = GpuSwap(reg_val, endianness);
  xe::store(memory_->TranslatePhysical(mem_addr), reg_val);
  trace_writer_.WriteMemoryWrite(CpuToGpu(mem_addr), 4);

  return true;
}

bool CommandProcessor::ExecutePacketType3_MEM_WRITE(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  uint32_t write_addr = reader->ReadAndSwap<uint32_t>();
  for (uint32_t i = 0; i < count - 1; i++) {
    uint32_t write_data = reader->ReadAndSwap<uint32_t>();

    auto endianness = static_cast<Endian>(write_addr & 0x3);
    auto addr = write_addr & ~0x3;
    write_data = GpuSwap(write_data, endianness);
    xe::store(memory_->TranslatePhysical(addr), write_data);
    trace_writer_.WriteMemoryWrite(CpuToGpu(addr), 4);
    write_addr += 4;
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t write_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t write_data = reader->ReadAndSwap<uint32_t>();
  uint32_t value;
  if (wait_info & 0x10) {
    // Memory.
    auto endianness = static_cast<Endian>(poll_reg_addr & 0x3);
    poll_reg_addr &= ~0x3;
    trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
    value = GpuSwap(value, endianness);
  } else {
    // Register.
    assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
    value = register_file_->values[poll_reg_addr].u32;
  }
  bool matched = false;
  switch (wait_info & 0x7) {
    case 0x0:  // Never.
      matched = false;
      break;
    case 0x1:  // Less than reference.
      matched = (value & mask) < ref;
      break;
    case 0x2:  // Less than or equal to reference.
      matched = (value & mask) <= ref;
      break;
    case 0x3:  // Equal to reference.
      matched = (value & mask) == ref;
      break;
    case 0x4:  // Not equal to reference.
      matched = (value & mask) != ref;
      break;
    case 0x5:  // Greater than or equal to reference.
      matched = (value & mask) >= ref;
      break;
    case 0x6:  // Greater than reference.
      matched = (value & mask) > ref;
      break;
    case 0x7:  // Always
      matched = true;
      break;
  }
  if (matched) {
    // Write.
    if (wait_info & 0x100) {
      // Memory.
      auto endianness = static_cast<Endian>(write_reg_addr & 0x3);
      write_reg_addr &= ~0x3;
      write_data = GpuSwap(write_data, endianness);
      xe::store(memory_->TranslatePhysical(write_reg_addr), write_data);
      trace_writer_.WriteMemoryWrite(CpuToGpu(write_reg_addr), 4);
    } else {
      // Register.
      WriteRegister(write_reg_addr, write_data);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader->AdvanceRead((count - 1) * sizeof(uint32_t));
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a VS|PS_done event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  uint32_t value = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  uint32_t data_value;
  if ((initiator >> 31) & 0x1) {
    // Write counter (GPU vblank counter?).
    data_value = counter_;
  } else {
    // Write value.
    data_value = value;
  }
  auto endianness = static_cast<Endian>(address & 0x3);
  address &= ~0x3;
  data_value = GpuSwap(data_value, endianness);
  xe::store(memory_->TranslatePhysical(address), data_value);
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), 4);
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_EXT(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a screen extent event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  auto endianness = static_cast<Endian>(address & 0x3);
  address &= ~0x3;

  // Let us hope we can fake this.
  // This callback tells the driver the xy coordinates affected by a previous
  // drawcall.
  // https://www.google.com/patents/US20060055701
  uint16_t extents[] = {
      0 >> 3,     // min x
      2560 >> 3,  // max x
      0 >> 3,     // min y
      2560 >> 3,  // max y
      0,          // min z
      1,          // max z
  };
  assert_true(endianness == Endian::k8in16);
  xe::copy_and_swap_16_unaligned(memory_->TranslatePhysical(address), extents,
                                 xe::countof(extents));
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  assert_true(count == 1);
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);

  // TODO: Flag the backend CP to write out zpass counters to
  // REG_RB_SAMPLE_COUNT_ADDR (probably # pixels passed depth test).
  // This applies to the last draw, I believe.

  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // initiate fetch of index buffer and draw
  // if dword0 != 0, this is a conditional draw based on viz query.
  // This ID matches the one issued in PM4_VIZ_QUERY
  // ID = dword0 & 0x3F;
  // use = dword0 & 0x40;
  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();  // viz query info
  uint32_t dword1 = reader->ReadAndSwap<uint32_t>();
  uint32_t index_count = dword1 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword1 & 0x3F);
  bool is_indexed = false;
  IndexBufferInfo index_buffer_info;
  uint32_t src_sel = (dword1 >> 6) & 0x3;
  if (src_sel == 0x0) {
    // DI_SRC_SEL_DMA
    // Indexed draw.
    is_indexed = true;
    index_buffer_info.guest_base = reader->ReadAndSwap<uint32_t>();
    uint32_t index_size = reader->ReadAndSwap<uint32_t>();
    index_buffer_info.endianness = static_cast<Endian>(index_size >> 30);
    index_size &= 0x00FFFFFF;
    bool index_32bit = (dword1 >> 11) & 0x1;
    index_buffer_info.format =
        index_32bit ? IndexFormat::kInt32 : IndexFormat::kInt16;
    index_size *= index_32bit ? 4 : 2;
    index_buffer_info.length = index_size;
    index_buffer_info.count = index_count;
  } else if (src_sel == 0x1) {
    // DI_SRC_SEL_IMMEDIATE
    assert_always();
  } else if (src_sel == 0x2) {
    // DI_SRC_SEL_AUTO_INDEX
    // Auto draw.
    index_buffer_info.guest_base = 0;
    index_buffer_info.length = 0;
  } else {
    // Invalid source select.
    assert_always();
  }

  bool success = IssueDraw(prim_type, index_count,
                           is_indexed ? &index_buffer_info : nullptr);
  if (!success) {
    XELOGE("PM4_DRAW_INDX(%d, %d, %d): Failed in backend", index_count,
           prim_type, src_sel);
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // draw using supplied indices in packet
  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();
  uint32_t index_count = dword0 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword0 & 0x3F);
  uint32_t src_sel = (dword0 >> 6) & 0x3;
  assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
  // Index buffer unused as automatic.
  // bool index_32bit = (dword0 >> 11) & 0x1;
  // uint32_t indices_size = index_count * (index_32bit ? 4 : 2);
  // uint32_t index_ptr = reader->ptr();
  reader->AdvanceRead((count - 1) * sizeof(uint32_t));

  bool success = IssueDraw(prim_type, index_count, nullptr);
  if (!success) {
    XELOGE("PM4_DRAW_INDX_IMM(%d, %d): Failed in backend", index_count,
           prim_type);
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      reader->AdvanceRead((count - 1) * sizeof(uint32_t));
      return true;
  }
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT2(RingBuffer* reader,
                                                        uint32_t packet,
                                                        uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  // load constants from memory
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader->ReadAndSwap<uint32_t>();
  size_dwords &= 0xFFF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      return true;
  }
  trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
  for (uint32_t n = 0; n < size_dwords; n++, index++) {
    uint32_t data = xe::load_and_swap<uint32_t>(
        memory_->TranslatePhysical(address + n * 4));
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_SHADER_CONSTANTS(
    RingBuffer* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->ReadAndSwap<uint32_t>();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  trace_writer_.WriteMemoryRead(CpuToGpu(addr), size_dwords * 4);
  auto shader =
      LoadShader(shader_type, addr, memory_->TranslatePhysical<uint32_t*>(addr),
                 size_dwords);
  switch (shader_type) {
    case ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();
  uint32_t dword1 = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  assert_true(reader->read_count() >= size_dwords * 4);
  assert_true(count - 2 >= size_dwords);
  auto shader =
      LoadShader(shader_type, uint32_t(reader->read_ptr()),
                 reinterpret_cast<uint32_t*>(reader->read_ptr()), size_dwords);
  switch (shader_type) {
    case ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  reader->AdvanceRead(size_dwords * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(RingBuffer* reader,
                                                           uint32_t packet,
                                                           uint32_t count) {
  // selective invalidation of state pointers
  /*uint32_t mask =*/reader->ReadAndSwap<uint32_t>();
  // driver_->InvalidateState(mask);
  return true;
}

bool CommandProcessor::ExecutePacketType3_VIZ_QUERY(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // begin/end initiator for viz query extent processing
  // https://www.google.com/patents/US20050195186
  assert_true(count == 1);

  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();

  uint32_t id = dword0 & 0x3F;
  uint32_t end = dword0 & 0x80;
  if (!end) {
    // begin a new viz query @ id
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_START);
    XELOGGPU("Begin viz query ID %.2X", id);
  } else {
    // end the viz query
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_END);
    XELOGGPU("End viz query ID %.2X", id);
  }

  return true;
}

}  // namespace gpu
}  // namespace xe
