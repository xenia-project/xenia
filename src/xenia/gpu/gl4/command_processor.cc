/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <xenia/gpu/gl4/command_processor.h>

#include <algorithm>

#include <poly/logging.h>
#include <xenia/gpu/gl4/gl4_graphics_system.h>
#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/xenos.h>

#include <third_party/xxhash/xxhash.h>

#define XETRACECP(fmt, ...) \
  if (FLAGS_trace_ring_buffer) XELOGGPU(fmt, ##__VA_ARGS__)

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

CommandProcessor::CommandProcessor(GL4GraphicsSystem* graphics_system)
    : memory_(graphics_system->memory()),
      membase_(graphics_system->memory()->membase()),
      graphics_system_(graphics_system),
      register_file_(graphics_system_->register_file()),
      worker_running_(true),
      time_base_(0),
      counter_(0),
      primary_buffer_ptr_(0),
      primary_buffer_size_(0),
      read_ptr_index_(0),
      read_ptr_update_freq_(0),
      read_ptr_writeback_ptr_(0),
      write_ptr_index_event_(CreateEvent(NULL, FALSE, FALSE, NULL)),
      write_ptr_index_(0),
      bin_select_(0xFFFFFFFFull),
      bin_mask_(0xFFFFFFFFull),
      active_vertex_shader_(nullptr),
      active_pixel_shader_(nullptr) {
  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  time_base_ = perf_counter.QuadPart;
}

CommandProcessor::~CommandProcessor() { CloseHandle(write_ptr_index_event_); }

uint64_t CommandProcessor::QueryTime() {
  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  return perf_counter.QuadPart - time_base_;
}

void CommandProcessor::Initialize(uint32_t ptr, uint32_t page_count) {
  primary_buffer_ptr_ = ptr;
  // Not sure this is correct, but it's a way to take the page_count back to
  // the number of bytes allocated by the physical alloc.
  uint32_t original_size = 1 << (0x1C - page_count - 1);
  primary_buffer_size_ = original_size;
  read_ptr_index_ = 0;

  worker_running_ = true;
  worker_thread_ = std::thread([this]() {
    poly::threading::set_name("GL4 Worker");
    xe::Profiler::ThreadEnter("GL4 Worker");
    WorkerMain();
    xe::Profiler::ThreadExit();
  });
}

void CommandProcessor::Shutdown() {
  worker_running_ = false;
  SetEvent(write_ptr_index_event_);
  worker_thread_.join();

  all_shaders_.clear();
  shader_cache_.clear();
}

void CommandProcessor::WorkerMain() {
  while (worker_running_) {
    uint32_t write_ptr_index = write_ptr_index_.load();
    while (write_ptr_index == 0xBAADF00D ||
           read_ptr_index_ == write_ptr_index) {
      // Check if the pointer has moved.
      // We wait a short bit here to yield time. Since we are also running the
      // main window display we don't want to pause too long, though.
      // YieldProcessor();
      const int wait_time_ms = 5;
      if (WaitForSingleObject(write_ptr_index_event_, wait_time_ms) ==
          WAIT_TIMEOUT) {
        write_ptr_index = write_ptr_index_.load();
        continue;
      }
    }
    assert_true(read_ptr_index_ != write_ptr_index);

    // Process the new commands.
    XETRACECP("Command processor thread work");

    // Execute. Note that we handle wraparound transparently.
    ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
    read_ptr_index_ = write_ptr_index;

    // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      poly::store_and_swap<uint32_t>(membase_ + read_ptr_writeback_ptr_,
                                     read_ptr_index_);
    }
  }
}

void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = (primary_buffer_ptr_ & ~0x1FFFFFFF) + ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, number of quadwords read between updates of the
  //              read pointer.
  read_ptr_update_freq_ = (uint32_t)pow(2.0, (double)block_size) / 4;
}

void CommandProcessor::UpdateWritePointer(uint32_t value) {
  write_ptr_index_ = value;
  SetEvent(write_ptr_index_event_);
}

void CommandProcessor::WriteRegister(uint32_t packet_ptr, uint32_t index,
                                     uint32_t value) {
  RegisterFile* regs = register_file_;
  assert_true(index < RegisterFile::kRegisterCount);
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
      poly::store_and_swap<uint32_t>(
          membase_ + xenos::GpuToCpu(primary_buffer_ptr_, mem_addr), value);
    }
  }
}

void CommandProcessor::MakeCoherent() {
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
  auto base_host = regs->values[XE_GPU_REG_COHER_BASE_HOST].u32;
  auto size_host = regs->values[XE_GPU_REG_COHER_SIZE_HOST].u32;

  if (!(status_host & 0x80000000ul)) {
    return;
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  XETRACECP("Make %.8X -> %.8X (%db) coherent", base_host,
            base_host + size_host, size_host);

  // Mark coherent.
  status_host &= ~0x80000000ul;
  regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32 = status_host;
}

class CommandProcessor::RingbufferReader {
 public:
  RingbufferReader(uint8_t* membase, uint32_t base_ptr, uint32_t ptr_mask,
                   uint32_t start_ptr, uint32_t end_ptr)
      : membase_(membase),
        base_ptr_(base_ptr),
        ptr_mask_(ptr_mask),
        start_ptr_(start_ptr),
        end_ptr_(end_ptr),
        ptr_(start_ptr) {}

  uint32_t ptr() const { return ptr_; }
  uint32_t offset() const { return (ptr_ - start_ptr_) / sizeof(uint32_t); }
  bool can_read() const { return ptr_ != end_ptr_; }

  uint32_t Peek() { return poly::load_and_swap<uint32_t>(membase_ + ptr_); }

  void CheckRead(uint32_t words) {
    assert_true(ptr_ + words * sizeof(uint32_t) <= end_ptr_);
  }

  uint32_t Read() {
    uint32_t value = poly::load_and_swap<uint32_t>(membase_ + ptr_);
    Advance(1);
    return value;
  }

  void Advance(uint32_t words) {
    ptr_ = ptr_ + words * sizeof(uint32_t);
    if (ptr_mask_) {
      ptr_ = base_ptr_ +
             (((ptr_ - base_ptr_) / sizeof(uint32_t)) & ptr_mask_) *
                 sizeof(uint32_t);
    }
    assert_true(ptr_ <= end_ptr_);
  }

  void Skip(uint32_t words) { Advance(words); }

  void TraceData(uint32_t words) {
    for (uint32_t i = 0; i < words; ++i) {
      uint32_t i_ptr = ptr_ + i * sizeof(uint32_t);
      XETRACECP("[%.8X]   %.8X", i_ptr,
                poly::load_and_swap<uint32_t>(membase_ + i_ptr));
    }
  }

 private:
  uint8_t* membase_;

  uint32_t base_ptr_;
  uint32_t ptr_mask_;
  uint32_t start_ptr_;
  uint32_t end_ptr_;
  uint32_t ptr_;
};

void CommandProcessor::ExecutePrimaryBuffer(uint32_t start_index,
                                            uint32_t end_index) {
  SCOPE_profile_cpu_f("gpu");

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + start_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + end_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  XETRACECP("[%.8X] ExecutePrimaryBuffer(%dw -> %dw)", start_ptr, start_index,
            end_index);

  // Execute commands!
  uint32_t ptr_mask = (primary_buffer_size_ / sizeof(uint32_t)) - 1;
  RingbufferReader reader(membase_, primary_buffer_ptr_, ptr_mask, start_ptr,
                          end_ptr);
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }
  if (end_index > start_index) {
    assert_true(reader.offset() == (end_index - start_index));
  }

  XETRACECP("           ExecutePrimaryBuffer End");
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t length) {
  XETRACECP("[%.8X] ExecuteIndirectBuffer(%dw)", ptr, length);

  // Execute commands!
  uint32_t ptr_mask = 0;
  RingbufferReader reader(membase_, primary_buffer_ptr_, ptr_mask, ptr,
                          ptr + length * sizeof(uint32_t));
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }

  XETRACECP("           ExecuteIndirectBuffer End");
}

bool CommandProcessor::ExecutePacket(RingbufferReader* reader) {
  RegisterFile* regs = register_file_;

  uint32_t packet_ptr = reader->ptr();
  const uint32_t packet = reader->Read();
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    XETRACECP("[%.8X] Packet(%.8X): 0?", packet_ptr, packet);
    return true;
  }

  switch (packet_type) {
    case 0x00:
      return ExecutePacketType0(reader, packet_ptr, packet);
    case 0x01:
      return ExecutePacketType1(reader, packet_ptr, packet);
    case 0x02:
      return ExecutePacketType2(reader, packet_ptr, packet);
    case 0x03:
      return ExecutePacketType3(reader, packet_ptr, packet);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

bool CommandProcessor::ExecutePacketType0(RingbufferReader* reader,
                                          uint32_t packet_ptr,
                                          uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).
  XETRACECP("[%.8X] Packet(%.8X): set registers:", packet_ptr, packet);
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  for (uint32_t m = 0; m < count; m++) {
    uint32_t reg_data = reader->Peek();
    uint32_t target_index = write_one_reg ? base_index : base_index + m;
    const char* reg_name = register_file_->GetRegisterName(target_index);
    XETRACECP("[%.8X]   %.8X -> %.4X %s", reader->ptr(), reg_data, target_index,
              reg_name ? reg_name : "");
    reader->Advance(1);
    WriteRegister(packet_ptr, target_index, reg_data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingbufferReader* reader,
                                          uint32_t packet_ptr,
                                          uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  XETRACECP("[%.8X] Packet(%.8X): set registers:", packet_ptr, packet);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_ptr_1 = reader->ptr();
  uint32_t reg_data_1 = reader->Read();
  uint32_t reg_ptr_2 = reader->ptr();
  uint32_t reg_data_2 = reader->Read();
  const char* reg_name_1 = register_file_->GetRegisterName(reg_index_1);
  const char* reg_name_2 = register_file_->GetRegisterName(reg_index_2);
  XETRACECP("[%.8X]   %.8X -> %.4X %s", reg_ptr_1, reg_data_1, reg_index_1,
            reg_name_1 ? reg_name_1 : "");
  XETRACECP("[%.8X]   %.8X -> %.4X %s", reg_ptr_2, reg_data_2, reg_index_2,
            reg_name_2 ? reg_name_2 : "");
  WriteRegister(packet_ptr, reg_index_1, reg_data_1);
  WriteRegister(packet_ptr, reg_index_2, reg_data_2);
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingbufferReader* reader,
                                          uint32_t packet_ptr,
                                          uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  XETRACECP("[%.8X] Packet(%.8X): padding", packet_ptr, packet);
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingbufferReader* reader,
                                          uint32_t packet_ptr,
                                          uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->offset();

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    if (!any_pass) {
      XETRACECP("[%.8X] Packet(%.8X): SKIPPED (predicate fail)", packet_ptr,
                packet);
      reader->Skip(count);
      return true;
    }
  }

  bool result = false;
  switch (opcode) {
    case PM4_ME_INIT:
      result = ExecutePacketType3_ME_INIT(reader, packet_ptr, packet, count);
      break;
    case PM4_NOP:
      result = ExecutePacketType3_NOP(reader, packet_ptr, packet, count);
      break;
    case PM4_INTERRUPT:
      result = ExecutePacketType3_INTERRUPT(reader, packet_ptr, packet, count);
      break;
    case PM4_XE_SWAP:
      result = ExecutePacketType3_XE_SWAP(reader, packet_ptr, packet, count);
      break;
    case PM4_INDIRECT_BUFFER:
      result =
          ExecutePacketType3_INDIRECT_BUFFER(reader, packet_ptr, packet, count);
      break;
    case PM4_WAIT_REG_MEM:
      result =
          ExecutePacketType3_WAIT_REG_MEM(reader, packet_ptr, packet, count);
      break;
    case PM4_REG_RMW:
      result = ExecutePacketType3_REG_RMW(reader, packet_ptr, packet, count);
      break;
    case PM4_COND_WRITE:
      result = ExecutePacketType3_COND_WRITE(reader, packet_ptr, packet, count);
      break;
    case PM4_EVENT_WRITE:
      result =
          ExecutePacketType3_EVENT_WRITE(reader, packet_ptr, packet, count);
      break;
    case PM4_EVENT_WRITE_SHD:
      result =
          ExecutePacketType3_EVENT_WRITE_SHD(reader, packet_ptr, packet, count);
      break;
    case PM4_DRAW_INDX:
      result = ExecutePacketType3_DRAW_INDX(reader, packet_ptr, packet, count);
      break;
    case PM4_DRAW_INDX_2:
      result =
          ExecutePacketType3_DRAW_INDX_2(reader, packet_ptr, packet, count);
      break;
    case PM4_SET_CONSTANT:
      result =
          ExecutePacketType3_SET_CONSTANT(reader, packet_ptr, packet, count);
      break;
    case PM4_LOAD_ALU_CONSTANT:
      result = ExecutePacketType3_LOAD_ALU_CONSTANT(reader, packet_ptr, packet,
                                                    count);
      break;
    case PM4_IM_LOAD:
      result = ExecutePacketType3_IM_LOAD(reader, packet_ptr, packet, count);
      break;
    case PM4_IM_LOAD_IMMEDIATE:
      result = ExecutePacketType3_IM_LOAD_IMMEDIATE(reader, packet_ptr, packet,
                                                    count);
      break;
    case PM4_INVALIDATE_STATE:
      result = ExecutePacketType3_INVALIDATE_STATE(reader, packet_ptr, packet,
                                                   count);
      break;

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->Read();
      XETRACECP("[%.8X] Packet(%.8X): PM4_SET_BIN_MASK_LO = %.8X", packet_ptr,
                packet, value);
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->Read();
      XETRACECP("[%.8X] Packet(%.8X): PM4_SET_BIN_MASK_HI = %.8X", packet_ptr,
                packet, value);
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->Read();
      XETRACECP("[%.8X] Packet(%.8X): PM4_SET_BIN_SELECT_LO = %.8X", packet_ptr,
                packet, value);
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->Read();
      XETRACECP("[%.8X] Packet(%.8X): PM4_SET_BIN_SELECT_HI = %.8X", packet_ptr,
                packet, value);
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;

    // Ignored packets - useful if breaking on the default handler below.
    case 0x50:  // 0xC0015000 usually 2 words, 0xFFFFFFFF / 0x00000000
      XETRACECP("[%.8X] Packet(%.8X): unknown!", packet_ptr, packet);
      reader->TraceData(count);
      reader->Skip(count);
      break;

    default:
      XETRACECP("[%.8X] Packet(%.8X): unknown!", packet_ptr, packet);
      reader->TraceData(count);
      reader->Skip(count);
      break;
  }

  assert_true(reader->offset() == data_start_offset + count);
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingbufferReader* reader,
                                                  uint32_t packet_ptr,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  XETRACECP("[%.8X] Packet(%.8X): PM4_ME_INIT", packet_ptr, packet);
  reader->TraceData(count);
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingbufferReader* reader,
                                              uint32_t packet_ptr,
                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  XETRACECP("[%.8X] Packet(%.8X): PM4_NOP", packet_ptr, packet);
  reader->TraceData(count);
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingbufferReader* reader,
                                                    uint32_t packet_ptr,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // generate interrupt from the command stream
  XETRACECP("[%.8X] Packet(%.8X): PM4_INTERRUPT", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t cpu_mask = reader->Read();
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingbufferReader* reader,
                                                  uint32_t packet_ptr,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  XETRACECP("[%.8X] Packet(%.8X): PM4_XE_SWAP", packet_ptr, packet);
  reader->TraceData(count);
  reader->Advance(count);
  if (swap_handler_) {
    swap_handler_();
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(
    RingbufferReader* reader, uint32_t packet_ptr, uint32_t packet,
    uint32_t count) {
  // indirect buffer dispatch
  uint32_t list_ptr = reader->Read();
  uint32_t list_length = reader->Read();
  XETRACECP("[%.8X] Packet(%.8X): PM4_INDIRECT_BUFFER %.8X (%dw)", packet_ptr,
            packet, list_ptr, list_length);
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingbufferReader* reader,
                                                       uint32_t packet_ptr,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // wait until a register or memory location is a specific value
  XETRACECP("[%.8X] Packet(%.8X): PM4_WAIT_REG_MEM", packet_ptr, packet);
  reader->TraceData(count);
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
      value =
          poly::load<uint32_t>(membase_ + GpuToCpu(packet_ptr, poll_reg_addr));
      value = GpuSwap(value, endianness);
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
        Sleep(wait / 0x100);
      } else {
        SwitchToThread();
      }
    }
  } while (!matched);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingbufferReader* reader,
                                                  uint32_t packet_ptr,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  XETRACECP("[%.8X] Packet(%.8X): PM4_REG_RMW", packet_ptr, packet);
  reader->TraceData(count);
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
  WriteRegister(packet_ptr, rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingbufferReader* reader,
                                                     uint32_t packet_ptr,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  XETRACECP("[%.8X] Packet(%.8X): PM4_COND_WRITE", packet_ptr, packet);
  reader->TraceData(count);
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
    value =
        poly::load<uint32_t>(membase_ + GpuToCpu(packet_ptr, poll_reg_addr));
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
      poly::store(membase_ + GpuToCpu(packet_ptr, write_reg_addr), write_data);
    } else {
      // Register.
      WriteRegister(packet_ptr, write_reg_addr, write_data);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingbufferReader* reader,
                                                      uint32_t packet_ptr,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  XETRACECP("[%.8X] Packet(%.8X): PM4_EVENT_WRITE (unimplemented!)", packet_ptr,
            packet);
  reader->TraceData(count);
  uint32_t initiator = reader->Read();
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
    RingbufferReader* reader, uint32_t packet_ptr, uint32_t packet,
    uint32_t count) {
  // generate a VS|PS_done event
  XETRACECP("[%.8X] Packet(%.8X): PM4_EVENT_WRITE_SHD", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t initiator = reader->Read();
  uint32_t address = reader->Read();
  uint32_t value = reader->Read();
  // Writeback initiator.
  WriteRegister(packet_ptr, XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
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
  poly::store(membase_ + GpuToCpu(address), data_value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingbufferReader* reader,
                                                    uint32_t packet_ptr,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // initiate fetch of index buffer and draw
  XETRACECP("[%.8X] Packet(%.8X): PM4_DRAW_INDX", packet_ptr, packet);
  reader->TraceData(count);
  // dword0 = viz query info
  uint32_t dword0 = reader->Read();
  uint32_t dword1 = reader->Read();
  uint32_t index_count = dword1 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword1 & 0x3F);
  uint32_t src_sel = (dword1 >> 6) & 0x3;
  if (src_sel == 0x0) {
    // Indexed draw.
    uint32_t index_base = reader->Read();
    uint32_t index_size = reader->Read();
    auto endianness = static_cast<Endian>(index_size >> 30);
    index_size &= 0x00FFFFFF;
    bool index_32bit = (dword1 >> 11) & 0x1;
    index_size *= index_32bit ? 4 : 2;
  } else if (src_sel == 0x2) {
    // Auto draw.
  } else {
    // Unknown source select.
    assert_always();
  }
  //  if (!driver_->PrepareDraw(draw_command_)) {
  //    draw_command_.prim_type = prim_type;
  //    draw_command_.start_index = 0;
  //    draw_command_.index_count = index_count;
  //    draw_command_.base_vertex = 0;
  //    if (src_sel == 0x0) {
  //      // Indexed draw.
  //      // TODO(benvanik): detect subregions of larger index
  //      buffers!
  //      driver_->PrepareDrawIndexBuffer(
  //          draw_command_, index_base, index_size,
  //          endianness,
  //          index_32bit ? INDEX_FORMAT_32BIT : INDEX_FORMAT_16BIT);
  //    } else if (src_sel == 0x2) {
  //      // Auto draw.
  //      draw_command_.index_buffer = nullptr;
  //    } else {
  //      // Unknown source select.
  //      assert_always();
  //    }
  //    driver_->Draw(draw_command_);
  //  } else {
  //    if (src_sel == 0x0) {
  //      reader->Advance(2);  // skip
  //    }
  //  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingbufferReader* reader,
                                                      uint32_t packet_ptr,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // draw using supplied indices in packet
  XETRACECP("[%.8X] Packet(%.8X): PM4_DRAW_INDX_2", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t dword0 = reader->Read();
  uint32_t index_count = dword0 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword0 & 0x3F);
  uint32_t src_sel = (dword0 >> 6) & 0x3;
  assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
  bool index_32bit = (dword0 >> 11) & 0x1;
  uint32_t indices_size = index_count * (index_32bit ? 4 : 2);
  reader->CheckRead(indices_size / sizeof(uint32_t));
  /*if (!driver_->PrepareDraw(draw_command_)) {
  draw_command_.prim_type = prim_type;
  draw_command_.start_index = 0;
  draw_command_.index_count = index_count;
  draw_command_.base_vertex = 0;
  draw_command_.index_buffer = nullptr;
  driver_->Draw(draw_command_);
  }*/
  reader->Advance(count - 1);
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingbufferReader* reader,
                                                       uint32_t packet_ptr,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  XETRACECP("[%.8X] Packet(%.8X): PM4_SET_CONSTANT", packet_ptr, packet);
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0x4:           // REGISTER
      index += 0x2000;  // registers
      for (uint32_t n = 0; n < count - 1; n++, index++) {
        uint32_t data = reader->Read();
        const char* reg_name = register_file_->GetRegisterName(index);
        XETRACECP("[%.8X]   %.8X -> %.4X %s", packet_ptr + (1 + n) * 4, data,
                  index, reg_name ? reg_name : "");
        WriteRegister(packet_ptr, index, data);
      }
      break;
    default:
      assert_always();
      break;
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(
    RingbufferReader* reader, uint32_t packet_ptr, uint32_t packet,
    uint32_t count) {
  // load constants from memory
  XETRACECP("[%.8X] Packet(%.8X): PM4_LOAD_ALU_CONSTANT", packet_ptr, packet);
  uint32_t address = reader->Read();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size = reader->Read();
  size &= 0xFFF;
  index += 0x4000;  // alu constants
  for (uint32_t n = 0; n < size; n++, index++) {
    uint32_t data = poly::load_and_swap<uint32_t>(
        membase_ + GpuToCpu(packet_ptr, address + n * 4));
    const char* reg_name = register_file_->GetRegisterName(index);
    XETRACECP("[%.8X]   %.8X -> %.4X %s", packet_ptr, data, index,
              reg_name ? reg_name : "");
    WriteRegister(packet_ptr, index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingbufferReader* reader,
                                                  uint32_t packet_ptr,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // load sequencer instruction memory (pointer-based)
  XETRACECP("[%.8X] Packet(%.8X): PM4_IM_LOAD", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t addr_type = reader->Read();
  auto shader_type = static_cast<ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->Read();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  LoadShader(shader_type,
             reinterpret_cast<uint32_t*>(membase_ + GpuToCpu(packet_ptr, addr)),
             size_dwords);
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(
    RingbufferReader* reader, uint32_t packet_ptr, uint32_t packet,
    uint32_t count) {
  // load sequencer instruction memory (code embedded in packet)
  XETRACECP("[%.8X] Packet(%.8X): PM4_IM_LOAD_IMMEDIATE", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t dword0 = reader->Read();
  uint32_t dword1 = reader->Read();
  auto shader_type = static_cast<ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  reader->CheckRead(size_dwords);
  LoadShader(shader_type, reinterpret_cast<uint32_t*>(membase_ + reader->ptr()),
             size_dwords);
  reader->Advance(size_dwords);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(
    RingbufferReader* reader, uint32_t packet_ptr, uint32_t packet,
    uint32_t count) {
  // selective invalidation of state pointers
  XETRACECP("[%.8X] Packet(%.8X): PM4_INVALIDATE_STATE", packet_ptr, packet);
  reader->TraceData(count);
  uint32_t mask = reader->Read();
  // driver_->InvalidateState(mask);
  return true;
}

bool CommandProcessor::LoadShader(ShaderType shader_type,
                                  const uint32_t* address,
                                  uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  GL4Shader* shader_ptr = nullptr;
  uint64_t hash = XXH64(address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_cache_.find(hash);
  if (it != shader_cache_.end()) {
    // Found in the cache.
    // TODO(benvanik): compare bytes? Likelyhood of collision is low.
    shader_ptr = it->second;
  } else {
    // Not found in cache.
    // No translation is performed here, as it depends on program_cntl.
    auto shader =
        std::make_unique<GL4Shader>(shader_type, hash, address, dword_count);
    shader_ptr = shader.get();
    shader_cache_.insert({hash, shader_ptr});
    all_shaders_.emplace_back(std::move(shader));

    XELOGGPU("Set %s shader at %0.8X (%db):\n%s",
             shader_type == ShaderType::kVertex ? "vertex" : "pixel",
             uint32_t(reinterpret_cast<uintptr_t>(address) -
                      reinterpret_cast<uintptr_t>(membase_)),
             dword_count * 4, shader_ptr->ucode_disassembly().c_str());
  }
  switch (shader_type) {
    case ShaderType::kVertex:
      active_vertex_shader_ = shader_ptr;
      break;
    case ShaderType::kPixel:
      active_pixel_shader_ = shader_ptr;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
