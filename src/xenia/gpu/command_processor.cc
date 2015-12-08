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

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"

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
  std::wstring path = root_path + L"stream";
  trace_state_ = TraceState::kStreaming;
  trace_writer_.Open(path);
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
    xe::FatalError("Unable to setup command processor GL state");
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
      SCOPE_profile_cpu_i("gpu", "xe::gpu::gl4::CommandProcessor::Stall");
      // We've run out of commands to execute.
      // We spin here waiting for new ones, as the overhead of waiting on our
      // event is too high.
      PrepareForWait();
      do {
        // TODO(benvanik): if we go longer than Nms, switch to waiting?
        // It'll keep us from burning power.
        // const int wait_time_ms = 5;
        // xe::threading::Wait(write_ptr_index_event_.get(), true,
        //                     std::chrono::milliseconds(wait_time_ms));
        xe::threading::MaybeYield();
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
    ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
    read_ptr_index_ = write_ptr_index;

    // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      xe::store_and_swap<uint32_t>(
          memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);
    }
  }

  ShutdownContext();
}

bool CommandProcessor::SetupContext() { return true; }

void CommandProcessor::ShutdownContext() { context_.reset(); }

void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  primary_buffer_ptr_ = ptr;
  // Not sure this is correct, but it's a way to take the page_count back to
  // the number of bytes allocated by the physical alloc.
  uint32_t original_size = 1 << (0x1C - page_count - 1);
  primary_buffer_size_ = original_size;
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

void CommandProcessor::MakeCoherent() {
  SCOPE_profile_cpu_f("gpu");

  // Status host often has 0x01000000 or 0x03000000.
  // This is likely toggling VC (vertex cache) or TC (texture cache).
  // Or, it also has a direction in here maybe - there is probably
  // some way to check for dest coherency (what all the COHER_DEST_BASE_*
  // registers are for).
  // Best docs I've found on this are here:
  // http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2013/10/R6xx_R7xx_3D.pdf
  // http://cgit.freedesktop.org/xorg/driver/xf86-video-radeonhd/tree/src/r6xx_accel.c?id=3f8b6eccd9dba116cc4801e7f80ce21a879c67d2#n454

  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;
  // auto base_host = regs->values[XE_GPU_REG_COHER_BASE_HOST].u32;
  // auto size_host = regs->values[XE_GPU_REG_COHER_SIZE_HOST].u32;

  if (!(status_host & 0x80000000ul)) {
    return;
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  // XELOGD("Make %.8X -> %.8X (%db) coherent", base_host, base_host +
  // size_host, size_host);

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
    while (true) {
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

class CommandProcessor::RingbufferReader {
 public:
  RingbufferReader(uint8_t* membase, uint32_t base_ptr, uint32_t ptr_mask,
                   uint32_t start_ptr, uint32_t end_ptr)
      : membase_(membase),
        base_ptr_(base_ptr),
        ptr_mask_(ptr_mask),
        end_ptr_(end_ptr),
        ptr_(start_ptr),
        offset_(0) {}

  uint32_t ptr() const { return ptr_; }
  uint32_t offset() const { return offset_; }
  bool can_read() const { return ptr_ != end_ptr_; }

  uint32_t Peek() { return xe::load_and_swap<uint32_t>(membase_ + ptr_); }

  void CheckRead(uint32_t words) {
    assert_true(ptr_ + words * sizeof(uint32_t) <= end_ptr_);
  }

  uint32_t Read() {
    uint32_t value = xe::load_and_swap<uint32_t>(membase_ + ptr_);
    Advance(1);
    return value;
  }

  void Advance(uint32_t words) {
    offset_ += words;
    ptr_ = ptr_ + words * sizeof(uint32_t);
    if (ptr_mask_) {
      ptr_ = base_ptr_ +
             (((ptr_ - base_ptr_) / sizeof(uint32_t)) & ptr_mask_) *
                 sizeof(uint32_t);
    }
  }

  void Skip(uint32_t words) { Advance(words); }

 private:
  uint8_t* membase_;

  uint32_t base_ptr_;
  uint32_t ptr_mask_;
  uint32_t end_ptr_;
  uint32_t ptr_;
  uint32_t offset_;
};

void CommandProcessor::ExecutePrimaryBuffer(uint32_t start_index,
                                            uint32_t end_index) {
  SCOPE_profile_cpu_f("gpu");

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + start_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + end_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, end_index - start_index);

  // Execute commands!
  uint32_t ptr_mask = (primary_buffer_size_ / sizeof(uint32_t)) - 1;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, start_ptr, end_ptr);
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }
  if (end_index > start_index) {
    assert_true(reader.offset() == (end_index - start_index));
  }

  trace_writer_.WritePrimaryBufferEnd();
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t length) {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.WriteIndirectBufferStart(ptr, length * sizeof(uint32_t));

  // Execute commands!
  uint32_t ptr_mask = 0;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, ptr, ptr + length * sizeof(uint32_t));
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }

  trace_writer_.WriteIndirectBufferEnd();
}

void CommandProcessor::ExecutePacket(uint32_t ptr, uint32_t count) {
  uint32_t ptr_mask = 0;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, ptr, ptr + count * sizeof(uint32_t));
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }
}

bool CommandProcessor::ExecutePacket(RingbufferReader* reader) {
  const uint32_t packet = reader->Read();
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 1);
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

bool CommandProcessor::ExecutePacketType0(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  trace_writer_.WritePacketStart(reader->ptr() - 4, 1 + count);

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  for (uint32_t m = 0; m < count; m++) {
    uint32_t reg_data = reader->Read();
    uint32_t target_index = write_one_reg ? base_index : base_index + m;
    WriteRegister(target_index, reg_data);
  }

  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(reader->ptr() - 4, 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader->Read();
  uint32_t reg_data_2 = reader->Read();
  WriteRegister(reg_index_1, reg_data_1);
  WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(reader->ptr() - 4, 1);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->offset();

  // To handle nesting behavior when tracing we special case indirect buffers.
  if (opcode == PM4_INDIRECT_BUFFER) {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 2);
  } else {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 1 + count);
  }

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  // We also skip predicated swaps, as they are never valid (probably?).
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    if (!any_pass || opcode == PM4_XE_SWAP) {
      reader->Skip(count);
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

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->Read();
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->Read();
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->Read();
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->Read();
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;

    // Ignored packets - useful if breaking on the default handler below.
    case 0x50:  // 0xC0015000 usually 2 words, 0xFFFFFFFF / 0x00000000
    case 0x51:  // 0xC0015100 usually 2 words, 0xFFFFFFFF / 0xFFFFFFFF
      reader->Skip(count);
      break;

    case 0x00:
      // Not a valid opcode. Something's up.
      XELOGGPU("Invalid GPU Opcode Detected 0x%x", 0x00);
      assert_always();
      break;

    default:
      XELOGGPU("Not Implemented GPU OPCODE: 0x%X\t\tCOUNT: %d\n", opcode,
               count);
      reader->Skip(count);
      break;
  }

  trace_writer_.WritePacketEnd();
  assert_true(reader->offset() == data_start_offset + count);
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingbufferReader* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingbufferReader* reader,
                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingbufferReader* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader->Read();
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingbufferReader* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  XELOGI("XE_SWAP");

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t magic = reader->Read();
  assert_true(magic == 'SWAP');

  // TODO(benvanik): only swap frontbuffer ptr.
  uint32_t frontbuffer_ptr = reader->Read();
  uint32_t frontbuffer_width = reader->Read();
  uint32_t frontbuffer_height = reader->Read();
  reader->Advance(count - 4);

  if (swap_mode_ == SwapMode::kNormal) {
    IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  }

  if (trace_writer_.is_open()) {
    trace_writer_.WriteEvent(EventType::kSwap);
    trace_writer_.Flush();
    if (trace_state_ == TraceState::kSingleFrame) {
      trace_state_ = TraceState::kDisabled;
      trace_writer_.Close();
    }
  } else if (trace_state_ == TraceState::kSingleFrame) {
    // New trace request - we only start tracing at the beginning of a frame.
    auto frame_number = L"frame_" + std::to_wstring(counter_);
    auto path = trace_frame_path_ + frame_number;
    trace_writer_.Open(path);
  }
  ++counter_;
  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // indirect buffer dispatch
  uint32_t list_ptr = CpuToGpu(reader->Read());
  uint32_t list_length = reader->Read() & 0xFFFFF;
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingbufferReader* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value
  uint32_t wait_info = reader->Read();
  uint32_t poll_reg_addr = reader->Read();
  uint32_t ref = reader->Read();
  uint32_t mask = reader->Read();
  uint32_t wait = reader->Read();
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
      } else {
        xe::threading::MaybeYield();
      }
    }
  } while (!matched);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingbufferReader* reader,

                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader->Read();
  uint32_t and_mask = reader->Read();
  uint32_t or_mask = reader->Read();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF].u32;
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF].u32;
  } else {
    // | imm
    value |= or_mask;
  }
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF].u32;
  } else {
    // & imm
    value &= and_mask;
  }
  WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_TO_MEM(RingbufferReader* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // Copy Register to Memory (?)
  // Count is 2, assuming a Register Addr and a Memory Addr.

  uint32_t reg_addr = reader->Read();
  uint32_t mem_addr = reader->Read();

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

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingbufferReader* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  uint32_t wait_info = reader->Read();
  uint32_t poll_reg_addr = reader->Read();
  uint32_t ref = reader->Read();
  uint32_t mask = reader->Read();
  uint32_t write_reg_addr = reader->Read();
  uint32_t write_data = reader->Read();
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

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingbufferReader* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader->Read();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader->Advance(count - 1);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // generate a VS|PS_done event
  uint32_t initiator = reader->Read();
  uint32_t address = reader->Read();
  uint32_t value = reader->Read();
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

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_EXT(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // generate a screen extent event
  uint32_t initiator = reader->Read();
  uint32_t address = reader->Read();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  auto endianness = static_cast<Endian>(address & 0x3);
  address &= ~0x3;
  // Let us hope we can fake this.
  uint16_t extents[] = {
      0 >> 3,     // min x
      2560 >> 3,  // max x
      0 >> 3,     // min y
      2560 >> 3,  // max y
      0,          // min z
      1,          // max z
  };
  assert_true(endianness == Endian::k8in16);
  xe::copy_and_swap_16_aligned(
      reinterpret_cast<uint16_t*>(memory_->TranslatePhysical(address)), extents,
      xe::countof(extents));
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingbufferReader* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // initiate fetch of index buffer and draw
  // dword0 = viz query info
  /*uint32_t dword0 =*/reader->Read();
  uint32_t dword1 = reader->Read();
  uint32_t index_count = dword1 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword1 & 0x3F);
  bool is_indexed = false;
  IndexBufferInfo index_buffer_info;
  uint32_t src_sel = (dword1 >> 6) & 0x3;
  if (src_sel == 0x0) {
    // Indexed draw.
    is_indexed = true;
    index_buffer_info.guest_base = reader->Read();
    uint32_t index_size = reader->Read();
    index_buffer_info.endianness = static_cast<Endian>(index_size >> 30);
    index_size &= 0x00FFFFFF;
    bool index_32bit = (dword1 >> 11) & 0x1;
    index_buffer_info.format =
        index_32bit ? IndexFormat::kInt32 : IndexFormat::kInt16;
    index_size *= index_32bit ? 4 : 2;
    index_buffer_info.length = index_size;
    index_buffer_info.count = index_count;
  } else if (src_sel == 0x2) {
    // Auto draw.
    index_buffer_info.guest_base = 0;
    index_buffer_info.length = 0;
  } else {
    // Unknown source select.
    assert_always();
  }
  return IssueDraw(prim_type, index_count,
                   is_indexed ? &index_buffer_info : nullptr);
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingbufferReader* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // draw using supplied indices in packet
  uint32_t dword0 = reader->Read();
  uint32_t index_count = dword0 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword0 & 0x3F);
  uint32_t src_sel = (dword0 >> 6) & 0x3;
  assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
  // Index buffer unused as automatic.
  // bool index_32bit = (dword0 >> 11) & 0x1;
  // uint32_t indices_size = index_count * (index_32bit ? 4 : 2);
  // uint32_t index_ptr = reader->ptr();
  reader->Advance(count - 1);
  return IssueDraw(prim_type, index_count, nullptr);
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingbufferReader* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->Read();
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
      reader->Skip(count - 1);
      return true;
  }
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT2(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // load constants from memory
  uint32_t address = reader->Read();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader->Read();
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
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingbufferReader* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader->Read();
  auto shader_type = static_cast<ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->Read();
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

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader->Read();
  uint32_t dword1 = reader->Read();
  auto shader_type = static_cast<ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  reader->CheckRead(size_dwords);
  auto shader = LoadShader(shader_type, reader->ptr(),
                           memory_->TranslatePhysical<uint32_t*>(reader->ptr()),
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
  reader->Advance(size_dwords);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // selective invalidation of state pointers
  /*uint32_t mask =*/reader->Read();
  // driver_->InvalidateState(mask);
  return true;
}

}  // namespace gpu
}  // namespace xe
