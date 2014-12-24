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
#include <poly/math.h>
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

extern "C" extern "C" GLEWContext* glewGetContext();

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
  std::memset(&draw_command_, 0, sizeof(draw_command_));
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

bool CommandProcessor::Initialize(std::unique_ptr<GLContext> context) {
  context_ = std::move(context);

  worker_running_ = true;
  worker_thread_ = std::thread([this]() {
    poly::threading::set_name("GL4 Worker");
    xe::Profiler::ThreadEnter("GL4 Worker");
    context_->MakeCurrent();
    WorkerMain();
    xe::Profiler::ThreadExit();
  });

  return true;
}

void CommandProcessor::Shutdown() {
  worker_running_ = false;
  SetEvent(write_ptr_index_event_);
  worker_thread_.join();
  context_.reset();

  all_shaders_.clear();
  shader_cache_.clear();
}

void CommandProcessor::WorkerMain() {
  if (!SetupGL()) {
    PFATAL("Unable to setup command processor GL state");
    return;
  }

  while (worker_running_) {
    uint32_t write_ptr_index = write_ptr_index_.load();
    while (write_ptr_index == 0xBAADF00D ||
           read_ptr_index_ == write_ptr_index) {
      // Check if the pointer has moved.
      // We wait a short bit here to yield time. Since we are also running the
      // main window display we don't want to pause too long, though.
      // YieldProcessor();
      PrepareForWait();
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

  ShutdownGL();
}

bool CommandProcessor::SetupGL() {
  // Uniform buffer that stores the per-draw state (constants, etc).
  glCreateBuffers(1, &uniform_data_buffer_);
  glBindBuffer(GL_UNIFORM_BUFFER, uniform_data_buffer_);
  glNamedBufferStorage(uniform_data_buffer_, 16 * 1024, nullptr,
                       GL_MAP_WRITE_BIT | GL_DYNAMIC_STORAGE_BIT);

  return true;
}

void CommandProcessor::ShutdownGL() {
  glDeleteBuffers(1, &uniform_data_buffer_);
}

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

void CommandProcessor::PrepareForWait() {
  SCOPE_profile_cpu_f("gpu");

  // TODO(benvanik): fences and fancy stuff. We should figure out a way to
  // make interrupt callbacks from the GPU so that we don't have to do a full
  // synchronize here.
  // glFlush();
  glFinish();
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
  SCOPE_profile_cpu_f("gpu");

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
  // 63 words here, but only the first has any data.
  reader->TraceData(1);
  uint32_t frontbuffer_ptr = reader->Read();
  // TODO(benvanik): something with the frontbuffer ptr.
  reader->Advance(count - 1);
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
        PrepareForWait();
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

  uint32_t index_base = 0;
  uint32_t index_size = 0;
  Endian index_endianness = Endian::kUnspecified;
  bool index_32bit = false;
  uint32_t src_sel = (dword1 >> 6) & 0x3;
  if (src_sel == 0x0) {
    // Indexed draw.
    index_base = reader->Read();
    index_size = reader->Read();
    index_endianness = static_cast<Endian>(index_size >> 30);
    index_size &= 0x00FFFFFF;
    index_32bit = (dword1 >> 11) & 0x1;
    index_size *= index_32bit ? 4 : 2;
  } else if (src_sel == 0x2) {
    // Auto draw.
  } else {
    // Unknown source select.
    assert_always();
  }

  PrepareDraw(&draw_command_);
  draw_command_.prim_type = prim_type;
  draw_command_.start_index = 0;
  draw_command_.index_count = index_count;
  draw_command_.base_vertex = 0;
  if (src_sel == 0x0) {
    // Indexed draw.
    draw_command_.index_buffer.address = membase_ + index_base;
    draw_command_.index_buffer.size = index_size;
    draw_command_.index_buffer.endianess = index_endianness;
    draw_command_.index_buffer.format =
        index_32bit ? IndexFormat::kInt32 : IndexFormat::kInt16;
  } else if (src_sel == 0x2) {
    // Auto draw.
    draw_command_.index_buffer.address = nullptr;
  } else {
    // Unknown source select.
    assert_always();
  }
  return IssueDraw(&draw_command_);
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
  uint32_t index_ptr = reader->ptr();
  reader->Advance(count - 1);
  PrepareDraw(&draw_command_);
  draw_command_.prim_type = prim_type;
  draw_command_.start_index = 0;
  draw_command_.index_count = index_count;
  draw_command_.base_vertex = 0;
  draw_command_.index_buffer.address = nullptr;
  return IssueDraw(&draw_command_);
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
  SCOPE_profile_cpu_f("gpu");

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

void CommandProcessor::PrepareDraw(DrawCommand* draw_command) {
  auto& regs = *register_file_;
  auto& cmd = *draw_command;

  // Reset the things we don't modify so that we have clean state.
  cmd.prim_type = PrimitiveType::kPointList;
  cmd.index_count = 0;
  cmd.index_buffer.address = nullptr;

  // Generic stuff.
  cmd.start_index = regs[XE_GPU_REG_VGT_INDX_OFFSET].u32;
  cmd.base_vertex = 0;
}

bool CommandProcessor::IssueDraw(DrawCommand* draw_command) {
  SCOPE_profile_cpu_f("gpu");
  auto& regs = *register_file_;
  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == ModeControl::kIgnore) {
    // Ignored.
    return true;
  }

  if (!UpdateRenderTargets(draw_command)) {
    PLOGE("Unable to setup render targets");
    return false;
  }

  if (enable_mode == ModeControl::kCopy) {
    // Special copy handling.
    return IssueCopy(draw_command);
  }

  if (!UpdateState(draw_command)) {
    PLOGE("Unable to setup render state");
    return false;
  }

  // if (!PopulateShaders(draw_command)) {
  //  XELOGE("Unable to prepare draw shaders");
  //  return false;
  //}
  // if (!PopulateSamplers(draw_command)) {
  //  XELOGE("Unable to prepare draw samplers");
  //  return false;
  //}

  // if (!PopulateIndexBuffer(draw_command)) {
  //  PLOGE("Unable to setup index buffer");
  //  return false;
  //}
  // if (!PopulateVertexBuffers(draw_command)) {
  //  XELOGE("Unable to setup vertex buffers");
  //  return false;
  //}

  // TODO(benvanik): draw.

  return true;
}

bool CommandProcessor::UpdateState(DrawCommand* draw_command) {
  // Much of this state machine is extracted from:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf

  auto& regs = *register_file_;

  union float4 {
    float v[4];
    struct {
      float x, y, z, w;
    };
  };
  struct UniformDataBlock {
    float4 window_offset;    // tx,ty,?,?
    float4 window_scissor;   // x0,y0,x1,y1
    float4 viewport_offset;  // tx,ty,tz,?
    float4 viewport_scale;   // sx,sy,sz,?
    // TODO(benvanik): vertex format xyzw?

    float4 alpha_test;  // alpha test enable, func, ref, ?

    // Register data from 0x4000 to 0x4927.
    // SHADER_CONSTANT_000_X...
    float4 float_consts[512];
    // SHADER_CONSTANT_FETCH_00_0...
    uint32_t fetch_consts[32 * 6];
    // SHADER_CONSTANT_BOOL_000_031...
    int32_t bool_consts[8];
    // SHADER_CONSTANT_LOOP_00...
    int32_t loop_consts[32];
  };
  static_assert(sizeof(UniformDataBlock) <= 16 * 1024,
                "Need <=16k uniform data");

  auto buffer_ptr = reinterpret_cast<UniformDataBlock*>(
      glMapNamedBufferRange(uniform_data_buffer_, 0, 16 * 1024,
                            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));
  if (!buffer_ptr) {
    PLOGE("Unable to map uniform data buffer");
    return false;
  }

  // Window parameters.
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  buffer_ptr->window_offset.x = float(window_offset & 0x7FFF);
  buffer_ptr->window_offset.y = float((window_offset >> 16) & 0x7FFF);
  uint32_t window_scissor_tl = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
  uint32_t window_scissor_br = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
  buffer_ptr->window_scissor.x = float(window_scissor_tl & 0x7FFF);
  buffer_ptr->window_scissor.y = float((window_scissor_tl >> 16) & 0x7FFF);
  buffer_ptr->window_scissor.z = float(window_scissor_br & 0x7FFF);
  buffer_ptr->window_scissor.w = float((window_scissor_br >> 16) & 0x7FFF);

  // Viewport scaling. Only enabled if the flags are all set.
  buffer_ptr->viewport_scale.x =
      regs[XE_GPU_REG_PA_CL_VPORT_XSCALE].f32;  // 640
  buffer_ptr->viewport_offset.x =
      regs[XE_GPU_REG_PA_CL_VPORT_XOFFSET].f32;  // 640
  buffer_ptr->viewport_scale.y =
      regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32;  // -360
  buffer_ptr->viewport_offset.y =
      regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32;  // 360
  buffer_ptr->viewport_scale.z = regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32;  // 1
  buffer_ptr->viewport_offset.z =
      regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET].f32;  // 0

  // Whether each of the viewport settings is enabled.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  uint32_t vte_control = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
  bool vport_xscale_enable = (vte_control & (1 << 0)) > 0;
  bool vport_xoffset_enable = (vte_control & (1 << 1)) > 0;
  bool vport_yscale_enable = (vte_control & (1 << 2)) > 0;
  bool vport_yoffset_enable = (vte_control & (1 << 3)) > 0;
  bool vport_zscale_enable = (vte_control & (1 << 4)) > 0;
  bool vport_zoffset_enable = (vte_control & (1 << 5)) > 0;
  assert_true(vport_xscale_enable == vport_yscale_enable ==
              vport_zscale_enable == vport_xoffset_enable ==
              vport_yoffset_enable == vport_zoffset_enable);
  // VTX_XY_FMT = true: the incoming X, Y have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  bool vtx_xy_fmt = (vte_control >> 8) & 0x1;
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  bool vtx_z_fmt = (vte_control >> 9) & 0x1;
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  bool vtx_w0_fmt = (vte_control >> 10) & 0x1;
  // TODO(benvanik): pass to shaders? disable transform? etc?
  glViewport(0, 0, 1280, 720);

  // Copy over all constants.
  // TODO(benvanik): partial updates, etc. We could use shader constant access
  // knowledge that we get at compile time to only upload those constants
  // required.
  std::memcpy(
      &buffer_ptr->float_consts, &regs[XE_GPU_REG_SHADER_CONSTANT_000_X].f32,
      sizeof(buffer_ptr->float_consts) + sizeof(buffer_ptr->fetch_consts) +
          sizeof(buffer_ptr->loop_consts) + sizeof(buffer_ptr->bool_consts));

  // Scissoring.
  int32_t screen_scissor_tl = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL].u32;
  int32_t screen_scissor_br = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR].u32;
  if (screen_scissor_tl != 0 && screen_scissor_br != 0x20002000) {
    glEnable(GL_SCISSOR_TEST);
    // TODO(benvanik): signed?
    int32_t screen_scissor_x = screen_scissor_tl & 0x7FFF;
    int32_t screen_scissor_y = (screen_scissor_tl >> 16) & 0x7FFF;
    int32_t screen_scissor_w = screen_scissor_br & 0x7FFF - screen_scissor_x;
    int32_t screen_scissor_h =
        (screen_scissor_br >> 16) & 0x7FFF - screen_scissor_y;
    glScissor(screen_scissor_x, screen_scissor_y, screen_scissor_w,
              screen_scissor_h);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }

  // Rasterizer state.
  uint32_t mode_control = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;
  if (draw_command->prim_type == PrimitiveType::kRectangleList) {
    // Rect lists aren't culled. There may be other things they skip too.
    glDisable(GL_CULL_FACE);
  } else {
    switch (mode_control & 0x3) {
      case 0:
        glDisable(GL_CULL_FACE);
        break;
      case 1:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        break;
      case 2:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        break;
    }
  }
  if (mode_control & 0x4) {
    glFrontFace(GL_CW);
  } else {
    glFrontFace(GL_CCW);
  }
  // TODO(benvanik): wireframe mode.
  // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Deprecated in GL, implemented in shader.
  // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
  uint32_t color_control = regs[XE_GPU_REG_RB_COLORCONTROL].u32;
  buffer_ptr->alpha_test.x =
      (color_control & 0x4) ? 1.0f : 0.0f;                // ALPAHTESTENABLE
  buffer_ptr->alpha_test.y = float(color_control & 0x3);  // ALPHAFUNC
  buffer_ptr->alpha_test.z = regs[XE_GPU_REG_RB_ALPHA_REF].f32;

  static const GLenum blend_map[] = {
      /*  0 */ GL_ZERO,
      /*  1 */ GL_ONE,
      /*  2 */ GL_ZERO,  // ?
      /*  3 */ GL_ZERO,  // ?
      /*  4 */ GL_SRC_COLOR,
      /*  5 */ GL_ONE_MINUS_SRC_COLOR,
      /*  6 */ GL_SRC_ALPHA,
      /*  7 */ GL_ONE_MINUS_SRC_ALPHA,
      /*  8 */ GL_DST_COLOR,
      /*  9 */ GL_ONE_MINUS_DST_COLOR,
      /* 10 */ GL_DST_ALPHA,
      /* 11 */ GL_ONE_MINUS_DST_ALPHA,
      /* 12 */ GL_CONSTANT_COLOR,
      /* 13 */ GL_ONE_MINUS_CONSTANT_COLOR,
      /* 14 */ GL_CONSTANT_ALPHA,
      /* 15 */ GL_ONE_MINUS_CONSTANT_ALPHA,
      /* 16 */ GL_SRC_ALPHA_SATURATE,
  };
  static const GLenum blend_op_map[] = {
      /*  0 */ GL_FUNC_ADD,
      /*  1 */ GL_FUNC_SUBTRACT,
      /*  2 */ GL_MIN,
      /*  3 */ GL_MAX,
      /*  4 */ GL_FUNC_REVERSE_SUBTRACT,
  };
  uint32_t color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32;
  uint32_t blend_control[4] = {
      regs[XE_GPU_REG_RB_BLENDCONTROL_0].u32,
      regs[XE_GPU_REG_RB_BLENDCONTROL_1].u32,
      regs[XE_GPU_REG_RB_BLENDCONTROL_2].u32,
      regs[XE_GPU_REG_RB_BLENDCONTROL_3].u32,
  };
  for (int n = 0; n < poly::countof(blend_control); n++) {
    // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
    auto src_blend = blend_map[(blend_control[n] & 0x0000001F) >> 0];
    // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
    auto dest_blend = blend_map[(blend_control[n] & 0x00001F00) >> 8];
    // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
    auto blend_op = blend_op_map[(blend_control[n] & 0x000000E0) >> 5];
    // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
    auto src_blend_alpha = blend_map[(blend_control[n] & 0x001F0000) >> 16];
    // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
    auto dest_blend_alpha = blend_map[(blend_control[n] & 0x1F000000) >> 24];
    // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
    auto blend_op_alpha = blend_op_map[(blend_control[n] & 0x00E00000) >> 21];
    // A2XX_RB_COLOR_MASK_WRITE_*
    uint32_t write_mask = (color_mask >> (n * 4)) & 0xF;
    // A2XX_RB_COLORCONTROL_BLEND_DISABLE ?? Can't find this!
    // Just guess based on actions.
    bool blend_enable =
        !((src_blend == GL_ONE) && (dest_blend == GL_ZERO) &&
          (blend_op == GL_FUNC_ADD) && (src_blend_alpha == GL_ONE) &&
          (dest_blend_alpha == GL_ZERO) && (blend_op_alpha == GL_FUNC_ADD));
    if (blend_enable) {
      glEnablei(GL_BLEND, n);
      glBlendEquationSeparatei(n, blend_op, blend_op_alpha);
      glBlendFuncSeparatei(n, src_blend, dest_blend, src_blend_alpha,
                           dest_blend_alpha);
    } else {
      glDisablei(GL_BLEND, n);
    }
  }
  float blend_color[4] = {
      regs[XE_GPU_REG_RB_BLEND_RED].f32, regs[XE_GPU_REG_RB_BLEND_GREEN].f32,
      regs[XE_GPU_REG_RB_BLEND_BLUE].f32, regs[XE_GPU_REG_RB_BLEND_ALPHA].f32,
  };
  glBlendColor(blend_color[0], blend_color[1], blend_color[2], blend_color[3]);

  static const GLenum compare_func_map[] = {
      /*  0 */ GL_NEVER,
      /*  1 */ GL_LESS,
      /*  2 */ GL_EQUAL,
      /*  3 */ GL_LEQUAL,
      /*  4 */ GL_GREATER,
      /*  5 */ GL_NOTEQUAL,
      /*  6 */ GL_GEQUAL,
      /*  7 */ GL_ALWAYS,
  };
  static const GLenum stencil_op_map[] = {
      /*  0 */ GL_KEEP,
      /*  1 */ GL_ZERO,
      /*  2 */ GL_REPLACE,
      /*  3 */ GL_INCR_WRAP,
      /*  4 */ GL_DECR_WRAP,
      /*  5 */ GL_INVERT,
      /*  6 */ GL_INCR,
      /*  7 */ GL_DECR,
  };
  uint32_t depth_control = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
  // A2XX_RB_DEPTHCONTROL_Z_ENABLE
  if (depth_control & 0x00000002) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  // A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE
  glDepthMask((depth_control & 0x00000004) ? GL_TRUE : GL_FALSE);
  // A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE
  // ?
  // A2XX_RB_DEPTHCONTROL_ZFUNC
  glDepthFunc(compare_func_map[(depth_control & 0x00000070) >> 4]);
  // A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE
  if (depth_control & 0x00000001) {
    glEnable(GL_STENCIL_TEST);
  } else {
    glDisable(GL_STENCIL_TEST);
  }
  uint32_t stencil_ref_mask = regs[XE_GPU_REG_RB_STENCILREFMASK].u32;
  // RB_STENCILREFMASK_STENCILREF
  uint32_t stencil_ref = (stencil_ref_mask & 0x000000FF);
  // RB_STENCILREFMASK_STENCILMASK
  uint32_t stencil_read_mask = (stencil_ref_mask & 0x0000FF00) >> 8;
  // RB_STENCILREFMASK_STENCILWRITEMASK
  glStencilMask((stencil_ref_mask & 0x00FF0000) >> 16);
  // A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE
  bool backface_enabled = (depth_control & 0x00000080) != 0;
  if (backface_enabled) {
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC
    glStencilFuncSeparate(GL_FRONT,
                          compare_func_map[(depth_control & 0x00000700) >> 8],
                          stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS
    glStencilOpSeparate(GL_FRONT,
                        stencil_op_map[(depth_control & 0x00003800) >> 11],
                        stencil_op_map[(depth_control & 0x000E0000) >> 17],
                        stencil_op_map[(depth_control & 0x0001C000) >> 14]);
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF
    glStencilFuncSeparate(GL_BACK,
                          compare_func_map[(depth_control & 0x00700000) >> 20],
                          stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF
    glStencilOpSeparate(GL_BACK,
                        stencil_op_map[(depth_control & 0x03800000) >> 23],
                        stencil_op_map[(depth_control & 0xE0000000) >> 29],
                        stencil_op_map[(depth_control & 0x1C000000) >> 26]);
  } else {
    // Backfaces disabled - treat backfaces as frontfaces.
    glStencilFunc(compare_func_map[(depth_control & 0x00000700) >> 8],
                  stencil_ref, stencil_read_mask);
    glStencilOp(stencil_op_map[(depth_control & 0x00003800) >> 11],
                stencil_op_map[(depth_control & 0x000E0000) >> 17],
                stencil_op_map[(depth_control & 0x0001C000) >> 14]);
  }

  glUnmapNamedBuffer(uniform_data_buffer_);

  return true;
}

bool CommandProcessor::UpdateRenderTargets(DrawCommand* draw_command) {
  auto& regs = *register_file_;

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((surface_info >> 16) & 0x3);

  // Get/create all color render targets, if we are using them.
  // In depth-only mode we don't need them.
  GLuint color_targets[4] = {0, 0, 0, 0};
  if (enable_mode == ModeControl::kColorDepth) {
    uint32_t color_info[4] = {
        regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
    };
    for (int n = 0; n < poly::countof(color_info); n++) {
      uint32_t color_base = color_info[n] & 0xFFF;
      auto color_format =
          static_cast<ColorRenderTargetFormat>((color_info[n] >> 16) & 0xF);
      color_targets[n] = GetColorRenderTarget(surface_pitch, surface_msaa,
                                              color_base, color_format);
    }
  }

  uint32_t depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
  uint32_t depth_base = depth_info & 0xFFF;
  auto depth_format =
      static_cast<DepthRenderTargetFormat>((depth_info >> 16) & 0x1);
  GLuint depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa,
                                             depth_base, depth_format);
  // TODO(benvanik): when a game switches does it expect to keep the same
  //     depth buffer contents?

  // Get/create a framebuffer with the required targets.
  GLuint framebuffer = GetFramebuffer(color_targets, depth_target);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

  return true;
}

bool CommandProcessor::IssueCopy(DrawCommand* draw_command) {
  auto& regs = *register_file_;

  // This is used to resolve surfaces, taking them from EDRAM render targets
  // to system memory. It can optionally clear color/depth surfaces, too.
  // The command buffer has stuff for actually doing this by drawing, however
  // we should be able to do it without that much easier.

  uint32_t copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  // Render targets 0-3, 4 = depth
  uint32_t copy_src_select = copy_control & 0x7;
  bool color_clear_enabled = (copy_control >> 8) & 0x1;
  bool depth_clear_enabled = (copy_control >> 9) & 0x1;
  auto copy_command = static_cast<CopyCommand>((copy_control >> 20) & 0x3);

  uint32_t copy_dest_info = regs[XE_GPU_REG_RB_COPY_DEST_INFO].u32;
  auto copy_dest_endian = static_cast<Endian128>(copy_dest_info & 0x7);
  uint32_t copy_dest_array = (copy_dest_info >> 3) & 0x1;
  assert_true(copy_dest_array == 0);
  uint32_t copy_dest_slice = (copy_dest_info >> 4) & 0x7;
  assert_true(copy_dest_slice == 0);
  auto copy_dest_format =
      static_cast<ColorFormat>((copy_dest_info >> 7) & 0x3F);
  uint32_t copy_dest_number = (copy_dest_info >> 13) & 0x7;
  assert_true(copy_dest_number == 0);
  uint32_t copy_dest_bias = (copy_dest_info >> 16) & 0x3F;
  assert_true(copy_dest_bias == 0);
  uint32_t copy_dest_swap = (copy_dest_info >> 25) & 0x1;

  uint32_t copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32;
  uint32_t copy_dest_pitch = regs[XE_GPU_REG_RB_COPY_DEST_PITCH].u32;
  uint32_t copy_dest_height = (copy_dest_pitch >> 16) & 0x3FFF;
  copy_dest_pitch &= 0x3FFF;

  // None of this is supported yet:
  uint32_t copy_surface_slice = regs[XE_GPU_REG_RB_COPY_SURFACE_SLICE].u32;
  assert_true(copy_surface_slice == 0);
  uint32_t copy_func = regs[XE_GPU_REG_RB_COPY_FUNC].u32;
  assert_true(copy_func == 0);
  uint32_t copy_ref = regs[XE_GPU_REG_RB_COPY_REF].u32;
  assert_true(copy_ref == 0);
  uint32_t copy_mask = regs[XE_GPU_REG_RB_COPY_MASK].u32;
  assert_true(copy_mask == 0);

  GLenum read_format;
  GLenum read_type;
  switch (copy_dest_format) {
    case ColorFormat::kColor_8_8_8_8:
      read_format = copy_dest_swap ? GL_BGRA : GL_RGBA;
      read_type = GL_UNSIGNED_BYTE;
      break;
    default:
      assert_unhandled_case(copy_dest_format);
      return false;
  }

  // TODO(benvanik): swap channel ordering on copy_dest_swap
  //                 Can we use GL swizzles for this?

  // Swap byte order during read.
  // TODO(benvanik): handle other endian modes.
  switch (copy_dest_endian) {
    case Endian128::kUnspecified:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
      break;
    case Endian128::k8in32:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
      break;
    default:
      assert_unhandled_case(copy_dest_endian);
      return false;
  }

  // Destination pointer in guest memory.
  // We have GL throw bytes directly into it.
  // TODO(benvanik): copy to staging texture then PBO back?
  void* ptr = membase_ + GpuToCpu(copy_dest_base);

  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t w = copy_dest_pitch;
  uint32_t h = copy_dest_height;
  switch (copy_command) {
    case CopyCommand::kConvert:
      if (copy_src_select <= 3) {
        // Source from a bound render target.
        glReadBuffer(GL_COLOR_ATTACHMENT0 + copy_src_select);
        glReadPixels(x, y, w, h, read_format, read_type, ptr);
      } else {
        // Source from the bound depth/stencil target.
        glReadPixels(x, y, w, h, GL_DEPTH_STENCIL, read_type, ptr);
      }
      break;
    case CopyCommand::kRaw:
    case CopyCommand::kConstantOne:
    case CopyCommand::kNull:
    default:
      assert_unhandled_case(copy_command);
      return false;
  }

  if (color_clear_enabled || depth_clear_enabled) {
    // Clear requested, so let's setup for that.
    uint32_t copy_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
    uint32_t copy_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
    uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
    assert_true(copy_color_clear == copy_color_clear_low);

    if (color_clear_enabled) {
      // Clear the render target we selected for copy.
      assert_true(copy_src_select < 3);
    }

    if (depth_clear_enabled) {
      // Clear the current depth buffer.
    }
  }

  return true;
}

GLuint CommandProcessor::GetColorRenderTarget(uint32_t pitch,
                                              MsaaSamples samples,
                                              uint32_t base,
                                              ColorRenderTargetFormat format) {
  // Because we don't know the height of anything, we allocate at full res.
  // At 2560x2560, it's impossible for EDRAM to fit anymore.
  uint32_t width = 2560;
  uint32_t height = 2560;

  // NOTE: we strip gamma formats down to normal ones.
  if (format == ColorRenderTargetFormat::k8888Gamma) {
    format = ColorRenderTargetFormat::k8888;
  }

  CachedColorRenderTarget* cached = nullptr;
  for (auto& it = cached_color_render_targets_.begin();
       it != cached_color_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->format == format) {
      return it->texture;
    }
  }
  cached_color_render_targets_.push_back(CachedColorRenderTarget());
  cached = &cached_color_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;

  GLenum internal_format;
  switch (format) {
    case ColorRenderTargetFormat::k8888:
    case ColorRenderTargetFormat::k8888Gamma:
      internal_format = GL_RGBA8;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

GLuint CommandProcessor::GetDepthRenderTarget(uint32_t pitch,
                                              MsaaSamples samples,
                                              uint32_t base,
                                              DepthRenderTargetFormat format) {
  uint32_t width = 2560;
  uint32_t height = 2560;

  CachedDepthRenderTarget* cached = nullptr;
  for (auto& it = cached_depth_render_targets_.begin();
       it != cached_depth_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->format == format) {
      return it->texture;
    }
  }
  cached_depth_render_targets_.push_back(CachedDepthRenderTarget());
  cached = &cached_depth_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;

  GLenum internal_format;
  switch (format) {
    case DepthRenderTargetFormat::kD24S8:
      internal_format = GL_DEPTH24_STENCIL8;
      break;
    case DepthRenderTargetFormat::kD24FS8:
    // TODO(benvanik): not supported in GL?
    default:
      assert_unhandled_case(format);
      return 0;
  }

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

GLuint CommandProcessor::GetFramebuffer(GLuint color_targets[4],
                                        GLuint depth_target) {
  CachedFramebuffer* cached = nullptr;
  for (auto& it = cached_framebuffers_.begin();
       it != cached_framebuffers_.end(); ++it) {
    if ((depth_target == -1u || it->depth_target == depth_target) &&
        (color_targets[0] == -1u || it->color_targets[0] == color_targets[0]) &&
        (color_targets[1] == -1u || it->color_targets[1] == color_targets[1]) &&
        (color_targets[2] == -1u || it->color_targets[2] == color_targets[2]) &&
        (color_targets[3] == -1u || it->color_targets[3] == color_targets[3])) {
      return it->framebuffer;
    }
  }
  cached_framebuffers_.push_back(CachedFramebuffer());
  cached = &cached_framebuffers_.back();
  glCreateFramebuffers(1, &cached->framebuffer);
  for (int i = 0; i < 4; ++i) {
    uint32_t color_target = color_targets[i];
    if (color_target == -1u) {
      color_target = 0;
    }
    cached->color_targets[i] = color_target;
    glNamedFramebufferTexture(cached->framebuffer, GL_COLOR_ATTACHMENT0 + i,
                              color_target, 0);
  }
  if (depth_target == -1u) {
    depth_target = 0;
  }
  cached->depth_target = depth_target;
  glNamedFramebufferTexture(cached->framebuffer, GL_DEPTH_STENCIL_ATTACHMENT,
                            depth_target, 0);
  return cached->framebuffer;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
