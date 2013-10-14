/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/ring_buffer_worker.h>

#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/xenos/packets.h>
#include <xenia/gpu/xenos/registers.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


RingBufferWorker::RingBufferWorker(xe_memory_ref memory) :
    memory_(memory), driver_(0) {
  write_ptr_index_event_ = CreateEvent(
      NULL, FALSE, FALSE, NULL);

  primary_buffer_ptr_     = 0;
  primary_buffer_size_    = 0;
  read_ptr_index_         = 0;
  read_ptr_update_freq_   = 0;
  read_ptr_writeback_ptr_ = 0;
  write_ptr_index_        = 0;
  write_ptr_max_index_    = 0;
}

RingBufferWorker::~RingBufferWorker() {
  SetEvent(write_ptr_index_event_);
  CloseHandle(write_ptr_index_event_);
}

void RingBufferWorker::Initialize(GraphicsDriver* driver,
                                  uint32_t ptr, uint32_t page_count) {
  driver_               = driver;
  primary_buffer_ptr_   = ptr;
  // Not sure this is correct, but it's a way to take the page_count back to
  // the number of bytes allocated by the physical alloc.
  uint32_t original_size = 1 << (0x1C - page_count - 1);
  primary_buffer_size_  = original_size;
  read_ptr_index_       = 0;

  // Tell the driver what to use for translation.
  driver_->set_address_translation(primary_buffer_ptr_ & ~0x1FFFFFFF);
}

void RingBufferWorker::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = (primary_buffer_ptr_ & ~0x1FFFFFFF) + ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, number of quadwords read between updates of the
  //              read pointer.
  read_ptr_update_freq_ = (uint32_t)pow(2.0, (double)block_size) / 4;
}

void RingBufferWorker::UpdateWritePointer(uint32_t value) {
  write_ptr_max_index_  = MAX(write_ptr_max_index_, value);
  write_ptr_index_      = value;
  SetEvent(write_ptr_index_event_);
}

void RingBufferWorker::Pump() {
  uint8_t* p = xe_memory_addr(memory_);

  if (write_ptr_index_ == 0xBAADF00D ||
      read_ptr_index_ == write_ptr_index_) {
    // Check if the pointer has moved.
    // We wait a short bit here to yield time. Since we are also running the
    // main window display we don't want to pause too long, though.
    const int wait_time_ms = 1;
    if (WaitForSingleObject(write_ptr_index_event_,
                            wait_time_ms) == WAIT_TIMEOUT) {
      return;
    }
  }

  // Bring local so we don't have to worry about them changing out from under
  // us.
  uint32_t write_ptr_index = write_ptr_index_;
  uint32_t write_ptr_max_index = write_ptr_max_index_;
  if (read_ptr_index_ == write_ptr_index) {
    return;
  }

  // Process the new commands.
  XELOGGPU("Ring buffer thread work");

  // Execute. Note that we handle wraparound transparently.
  ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
  read_ptr_index_ = write_ptr_index;

  // TODO(benvanik): use read_ptr_update_freq_ and only issue after moving
  //     that many indices.
  if (read_ptr_writeback_ptr_) {
    XESETUINT32BE(p + read_ptr_writeback_ptr_, read_ptr_index_);
  }
}

void RingBufferWorker::ExecutePrimaryBuffer(
    uint32_t start_index, uint32_t end_index) {
  // Adjust pointer base.
  uint32_t ptr = primary_buffer_ptr_ + start_index * 4;
  ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + end_index * 4;
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  XELOGGPU("[%.8X] ExecutePrimaryBuffer(%dw -> %dw)",
           start_index, end_index);

  // Execute commands!
  PacketArgs args;
  args.ptr          = ptr;
  args.base_ptr     = primary_buffer_ptr_;
  args.max_address  = primary_buffer_ptr_ + primary_buffer_size_ * 4;
  args.ptr_mask     = (primary_buffer_size_ / 4) - 1;
  uint32_t n = 0;
  while (args.ptr != end_ptr) {
    n += ExecutePacket(args);
  }
  if (end_index > start_index) {
    XEASSERT(n == (end_index - start_index));
  }

  XELOGGPU("           ExecutePrimaryBuffer End");
}

void RingBufferWorker::ExecuteIndirectBuffer(uint32_t ptr, uint32_t length) {
  // Adjust pointer base.
  ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (ptr & 0x1FFFFFFF);

  XELOGGPU("[%.8X] ExecuteIndirectBuffer(%dw)", ptr, length);

  // Execute commands!
  PacketArgs args;
  args.ptr          = ptr;
  args.base_ptr     = ptr;
  args.max_address  = ptr + length * 4;
  args.ptr_mask     = 0;
  for (uint32_t n = 0; n < length;) {
    n += ExecutePacket(args);
    XEASSERT(n <= length);
  }

  XELOGGPU("           ExecuteIndirectBuffer End");
}

#define LOG_DATA(count) \
  for (uint32_t __m = 0; __m < count; __m++) { \
    XELOGGPU("[%.8X]   %.8X", \
             packet_ptr + (1 + __m) * 4, \
             XEGETUINT32BE(packet_base + 1 * 4 + __m * 4)); \
  }

#define TRANSLATE_ADDR(p) \
  ((p & ~0x3) + (primary_buffer_ptr_ & ~0x1FFFFFFF))

void RingBufferWorker::AdvancePtr(PacketArgs& args, uint32_t n) {
  args.ptr = args.ptr + n * 4;
  if (args.ptr_mask) {
    args.ptr =
        args.base_ptr + (((args.ptr - args.base_ptr) / 4) & args.ptr_mask) * 4;
  }
}
#define ADVANCE_PTR(n) AdvancePtr(args, n)
#define READ_PTR() \
  XEGETUINT32BE(p + args.ptr)
#define READ_AND_ADVANCE_PTR() \
  XEGETUINT32BE(p + args.ptr); ADVANCE_PTR(1);

uint32_t RingBufferWorker::ExecutePacket(PacketArgs& args) {
  uint8_t* p = xe_memory_addr(memory_);
  RegisterFile* regs = driver_->register_file();

  uint32_t packet_ptr = args.ptr;
  const uint8_t* packet_base = p + packet_ptr;
  const uint32_t packet = READ_PTR();
  ADVANCE_PTR(1);
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    return 1;
  }

  switch (packet_type) {
  case 0x00:
    {
      // Type-0 packet.
      // Write count registers in sequence to the registers starting at
      // (base_index << 2).
      XELOGGPU("[%.8X] Packet(%.8X): set registers:",
               packet_ptr, packet);
      uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      uint32_t base_index = (packet & 0xFFFF);
      for (uint32_t m = 0; m < count; m++) {
        uint32_t reg_data = READ_PTR();
        const char* reg_name = xenos::GetRegisterName(base_index + m);
        XELOGGPU("[%.8X]   %.8X -> %.4X %s",
                 args.ptr,
                 reg_data, base_index + m, reg_name ? reg_name : "");
        ADVANCE_PTR(1);
        // TODO(benvanik): exec write handler (if special).
        if (base_index + m < kXEGpuRegisterCount) {
          regs->values[base_index + m].u32 = reg_data;
        }
      }
      return 1 + count;
    }
    break;
  case 0x01:
    {
      // Type-1 packet.
      // Contains two registers of data. Type-0 should be more common.
      XELOGGPU("[%.8X] Packet(%.8X): set registers:",
               packet_ptr, packet);
      uint32_t reg_index_1 = packet & 0x7FF;
      uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
      uint32_t reg_ptr_1 = args.ptr;
      uint32_t reg_data_1 = READ_AND_ADVANCE_PTR();
      uint32_t reg_ptr_2 = args.ptr;
      uint32_t reg_data_2 = READ_AND_ADVANCE_PTR();
      const char* reg_name_1 = xenos::GetRegisterName(reg_index_1);
      const char* reg_name_2 = xenos::GetRegisterName(reg_index_2);
      XELOGGPU("[%.8X]   %.8X -> %.4X %s",
               reg_ptr_1,
               reg_data_1, reg_index_1, reg_name_1 ? reg_name_1 : "");
      XELOGGPU("[%.8X]   %.8X -> %.4X %s",
               reg_ptr_2,
               reg_data_2, reg_index_2, reg_name_2 ? reg_name_2 : "");
      // TODO(benvanik): exec write handler (if special).
      if (reg_index_1 < kXEGpuRegisterCount) {
        regs->values[reg_index_1].u32 = reg_data_1;
      }
      if (reg_index_2 < kXEGpuRegisterCount) {
        regs->values[reg_index_2].u32 = reg_data_2;
      }
      return 1 + 2;
    }
    break;
  case 0x02:
    // Type-2 packet.
    // No-op. Do nothing.
    return 1;
  case 0x03:
    {
      // Type-3 packet.
      uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      uint32_t opcode = (packet >> 8) & 0x7F;
      // & 1 == predicate, maybe?

      switch (opcode) {
      case PM4_ME_INIT:
        // initialize CP's micro-engine
        XELOGGPU("[%.8X] Packet(%.8X): PM4_ME_INIT",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_NOP:
        // skip N 32-bit words to get to the next packet
        // No-op, ignore some data.
        XELOGGPU("[%.8X] Packet(%.8X): PM4_NOP",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_INDIRECT_BUFFER:
        // indirect buffer dispatch
        {
          uint32_t list_ptr = READ_AND_ADVANCE_PTR();
          uint32_t list_length = READ_AND_ADVANCE_PTR();
          XELOGGPU("[%.8X] Packet(%.8X): PM4_INDIRECT_BUFFER %.8X (%dw)",
                   packet_ptr, packet, list_ptr, list_length);
          ExecuteIndirectBuffer(list_ptr, list_length);
        }
        break;

      case PM4_WAIT_REG_MEM:
        // wait until a register or memory location is a specific value
        XELOGGPU("[%.8X] Packet(%.8X): PM4_WAIT_REG_MEM",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_REG_RMW:
        // register read/modify/write
        // ? (used during shader upload and edram setup)
        XELOGGPU("[%.8X] Packet(%.8X): PM4_REG_RMW",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_COND_WRITE:
        // conditional write to memory or register
        XELOGGPU("[%.8X] Packet(%.8X): PM4_COND_WRITE",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_EVENT_WRITE:
        // generate an event that creates a write to memory when completed
        XELOGGPU("[%.8X] Packet(%.8X): PM4_EVENT_WRITE",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;
      case PM4_EVENT_WRITE_SHD:
        // generate a VS|PS_done event
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_EVENT_WRITE_SHD",
                   packet_ptr, packet);
          LOG_DATA(count);
          uint32_t d0 = READ_AND_ADVANCE_PTR(); // 3?
          uint32_t d1 = READ_AND_ADVANCE_PTR(); // ptr
          uint32_t d2 = READ_AND_ADVANCE_PTR(); // value?
          XESETUINT32BE(p + TRANSLATE_ADDR(d1), d2);
        }
        break;

      case PM4_DRAW_INDX:
        // initiate fetch of index buffer and draw
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_DRAW_INDX",
                   packet_ptr, packet);
          LOG_DATA(count);
          // d0 = viz query info
          uint32_t d0 = READ_AND_ADVANCE_PTR();
          uint32_t d1 = READ_AND_ADVANCE_PTR();
          uint32_t index_count = d1 >> 16;
          uint32_t prim_type = d1 & 0x3F;
          uint32_t src_sel = (d1 >> 6) & 0x3;
          XEASSERT(src_sel == 0x2); // 'SrcSel=AutoIndex'
          driver_->DrawIndexAuto(
              (XE_GPU_PRIMITIVE_TYPE)prim_type,
              index_count);
        }
        break;
      case PM4_DRAW_INDX_2:
        // draw using supplied indices in packet
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_DRAW_INDX_2",
                   packet_ptr, packet);
          LOG_DATA(count);
          uint32_t d0 = READ_AND_ADVANCE_PTR();
          uint32_t index_count = d0 >> 16;
          uint32_t prim_type = d0 & 0x3F;
          uint32_t src_sel = (d0 >> 6) & 0x3;
          XEASSERT(src_sel == 0x2); // 'SrcSel=AutoIndex'
          driver_->DrawIndexAuto(
              (XE_GPU_PRIMITIVE_TYPE)prim_type,
              index_count);
        }
        break;

      case PM4_IM_LOAD:
        // load sequencer instruction memory (pointer-based)
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_IM_LOAD",
                   packet_ptr, packet);
          LOG_DATA(count);
          uint32_t addr_type = READ_AND_ADVANCE_PTR();
          uint32_t type = addr_type & 0x3;
          uint32_t addr = addr_type & ~0x3;
          uint32_t start_size = READ_AND_ADVANCE_PTR();
          uint32_t start = start_size >> 16;
          uint32_t size = start_size & 0xFFFF; // dwords
          XEASSERT(start == 0);
          driver_->SetShader(
              (XE_GPU_SHADER_TYPE)type,
              TRANSLATE_ADDR(addr),
              start,
              size * 4);
        }
        break;
      case PM4_IM_LOAD_IMMEDIATE:
        // load sequencer instruction memory (code embedded in packet)
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_IM_LOAD_IMMEDIATE",
                   packet_ptr, packet);
          LOG_DATA(count);
          uint32_t type = READ_AND_ADVANCE_PTR();
          uint32_t start_size = READ_AND_ADVANCE_PTR();
          uint32_t start = start_size >> 16;
          uint32_t size = start_size & 0xFFFF; // dwords
          XEASSERT(start == 0);
          // TODO(benvanik): figure out if this could wrap.
          XEASSERT(args.ptr + size * 4 < args.max_address);
          driver_->SetShader(
              (XE_GPU_SHADER_TYPE)type,
              args.ptr,
              start,
              size * 4);
          ADVANCE_PTR(size);
        }
        break;

      case PM4_INVALIDATE_STATE:
        // selective invalidation of state pointers
        {
          XELOGGPU("[%.8X] Packet(%.8X): PM4_INVALIDATE_STATE",
                   packet_ptr, packet);
          LOG_DATA(count);
          uint32_t mask = READ_AND_ADVANCE_PTR();
          driver_->InvalidateState(mask);
        }
        break;

      default:
        XELOGGPU("[%.8X] Packet(%.8X): unknown!",
                 packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;
      }

      return 1 + count;
    }
    break;
  }

  return 0;
}
