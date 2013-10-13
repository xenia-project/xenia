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
  write_ptr_index_ = value;
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
  if (read_ptr_index_ == write_ptr_index) {
    return;
  }

  // Process the new commands.
  XELOGGPU("Ring buffer thread work");

  // Handle wrapping around.
  // TODO(benvanik): verify this is correct.
  if (write_ptr_index_ < read_ptr_index_) {
    // We wrapped. Execute all instructions until the end and go back to 0.
    XELOGGPU("Ring buffer wrapped back to zero (read %0.8X, write %0.8X)",
             read_ptr_index_, write_ptr_index);
    uint32_t pre_length = (primary_buffer_size_ / 4) - read_ptr_index_;
    if (pre_length) {
      ExecuteSegment(primary_buffer_ptr_ + read_ptr_index_ * 4, pre_length);
    }
    read_ptr_index_ = 0;
  }

  uint32_t length = write_ptr_index - read_ptr_index_;
  if (length) {
    ExecuteSegment(primary_buffer_ptr_ + read_ptr_index_ * 4, length);
    read_ptr_index_ = write_ptr_index;
  }

  // TODO(benvanik): use read_ptr_update_freq_ and only issue after moving
  //     that many indices.
  if (read_ptr_writeback_ptr_) {
    XESETUINT32BE(p + read_ptr_writeback_ptr_, read_ptr_index_);
  }
}

void RingBufferWorker::ExecuteSegment(uint32_t ptr, uint32_t length) {
  uint8_t* p = xe_memory_addr(memory_);
  RegisterFile* regs = driver_->register_file();

  // Adjust pointer base.
  ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (ptr & 0x1FFFFFFF);

  // Tell the driver what to use for translation.
  driver_->set_address_translation(primary_buffer_ptr_ & ~0x1FFFFFFF);

#define LOG_DATA(count) \
  for (uint32_t __m = 0; __m < count; __m++) { \
    XELOGGPU("[%.8X]   %.8X", \
             ptr + (n + 1 + __m) * 4, \
             XEGETUINT32BE(packet_base + 1 * 4 + __m * 4)); \
  }
#define TRANSLATE_ADDR(p) \
  ((p & ~0x3) + (primary_buffer_ptr_ & ~0x1FFFFFFF))

  XELOGGPU("[%.8X] CommandList(): executing %dw", ptr, length);

  // Execute commands!
  for (uint32_t n = 0; n < length;) {
    const uint8_t* packet_base = p + ptr + n * 4;
    const uint32_t packet = XEGETUINT32BE(packet_base);
    const uint32_t packet_type = packet >> 30;

    if (packet == 0) {
      n++;
      continue;
    }

    switch (packet_type) {
    case 0x00:
      {
        // Type-0 packet.
        // Write count registers in sequence to the registers starting at
        // (base_index << 2).
        XELOGGPU("[%.8X] Packet(%.8X): set registers:", ptr + n * 4,
                  ptr + n * 4, packet);
        uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
        uint32_t base_index = (packet & 0xFFFF);
        for (uint32_t m = 0; m < count; m++) {
          uint32_t reg_data = XEGETUINT32BE(packet_base + 1 * 4 + m * 4);
          const char* reg_name = xenos::GetRegisterName(base_index + m);
          XELOGGPU("[%.8X]   %.8X -> %.4X %s",
                   ptr + (n + 1 + m) * 4,
                   reg_data, base_index + m, reg_name ? reg_name : "");
          // TODO(benvanik): exec write handler (if special).
          if (base_index + m < kXEGpuRegisterCount) {
            regs->values[base_index + m].u32 = reg_data;
          }
        }
        n += 1 + count;
      }
      break;
    case 0x01:
      {
        // Type-1 packet.
        // Contains two registers of data. Type-0 should be more common.
        XELOGGPU("[%.8X] Packet(%.8X): set registers:",
                 ptr + n * 4, packet);
        uint32_t reg_index_1 = packet & 0x7FF;
        uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
        uint32_t reg_data_1 = XEGETUINT32BE(packet_base + 1 * 4);
        uint32_t reg_data_2 = XEGETUINT32BE(packet_base + 2 * 4);
        const char* reg_name_1 = xenos::GetRegisterName(reg_index_1);
        const char* reg_name_2 = xenos::GetRegisterName(reg_index_2);
        XELOGGPU("[%.8X]   %.8X -> %.4X %s",
                 ptr + (n + 1) * 4,
                 reg_data_1, reg_index_1, reg_name_1 ? reg_name_1 : "");
        XELOGGPU("[%.8X]   %.8X -> %.4X %s",
                 ptr + (n + 2) * 4,
                 reg_data_2, reg_index_2, reg_name_2 ? reg_name_2 : "");
        // TODO(benvanik): exec write handler (if special).
        if (reg_index_1 < kXEGpuRegisterCount) {
          regs->values[reg_index_1].u32 = reg_data_1;
        }
        if (reg_index_2 < kXEGpuRegisterCount) {
          regs->values[reg_index_2].u32 = reg_data_2;
        }

        n += 1 + 2;
      }
      break;
    case 0x02:
      // Type-2 packet.
      // No-op. Do nothing.
      n++;
      break;
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
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;

        case PM4_NOP:
          // skip N 32-bit words to get to the next packet
          // No-op, ignore some data.
          XELOGGPU("[%.8X] Packet(%.8X): PM4_NOP",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;

        case PM4_INDIRECT_BUFFER:
          // indirect buffer dispatch
          {
            uint32_t list_ptr = XEGETUINT32BE(packet_base + 1 * 4);
            uint32_t list_length = XEGETUINT32BE(packet_base + 2 * 4);
            XELOGGPU("[%.8X] Packet(%.8X): PM4_INDIRECT_BUFFER %.8X (%dw)",
                     ptr + n * 4, packet, list_ptr, list_length);
            ExecuteSegment(list_ptr, list_length);
            driver_->set_address_translation(primary_buffer_ptr_ & ~0x1FFFFFFF);
          }
          break;

        case PM4_WAIT_REG_MEM:
          // wait until a register or memory location is a specific value
          XELOGGPU("[%.8X] Packet(%.8X): PM4_WAIT_REG_MEM",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;

        case PM4_REG_RMW:
          // register read/modify/write
          // ? (used during shader upload and edram setup)
          XELOGGPU("[%.8X] Packet(%.8X): PM4_REG_RMW",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;

        case PM4_COND_WRITE:
          // conditional write to memory or register
          XELOGGPU("[%.8X] Packet(%.8X): PM4_COND_WRITE",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;

        case PM4_EVENT_WRITE:
          // generate an event that creates a write to memory when completed
          XELOGGPU("[%.8X] Packet(%.8X): PM4_EVENT_WRITE",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;
        case PM4_EVENT_WRITE_SHD:
          // generate a VS|PS_done event
          {
            XELOGGPU("[%.8X] Packet(%.8X): PM4_EVENT_WRITE_SHD",
                     ptr + n * 4, packet);
            LOG_DATA(count);
            // 3?
            uint32_t d0 = XEGETUINT32BE(packet_base + 1 * 4);
            // ptr
            uint32_t d1 = XEGETUINT32BE(packet_base + 2 * 4);
            // value?
            uint32_t d2 = XEGETUINT32BE(packet_base + 3 * 4);
            XESETUINT32BE(p + TRANSLATE_ADDR(d1), d2);
          }
          break;

        case PM4_DRAW_INDX:
          // initiate fetch of index buffer and draw
          {
            XELOGGPU("[%.8X] Packet(%.8X): PM4_DRAW_INDX",
                     ptr + n * 4, packet);
            LOG_DATA(count);
            // d0 = viz query info
            uint32_t d0 = XEGETUINT32BE(packet_base + 1 * 4);
            uint32_t d1 = XEGETUINT32BE(packet_base + 2 * 4);
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
                     ptr + n * 4, packet);
            LOG_DATA(count);
            uint32_t d0 = XEGETUINT32BE(packet_base + 1 * 4);
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
                     ptr + n * 4, packet);
            LOG_DATA(count);
            uint32_t addr_type = XEGETUINT32BE(packet_base + 1 * 4);
            uint32_t type = addr_type & 0x3;
            uint32_t addr = addr_type & ~0x3;
            uint32_t start_size = XEGETUINT32BE(packet_base + 2 * 4);
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
                     ptr + n * 4, packet);
            uint32_t type = XEGETUINT32BE(packet_base + 1 * 4);
            uint32_t start_size = XEGETUINT32BE(packet_base + 2 * 4);
            uint32_t start = start_size >> 16;
            uint32_t size = start_size & 0xFFFF; // dwords
            XEASSERT(start == 0);
            LOG_DATA(count);
            driver_->SetShader(
                (XE_GPU_SHADER_TYPE)type,
                ptr + n * 4 + 3 * 4,
                start,
                size * 4);
          }
          break;

        case PM4_INVALIDATE_STATE:
          // selective invalidation of state pointers
          {
            XELOGGPU("[%.8X] Packet(%.8X): PM4_INVALIDATE_STATE",
                     ptr + n * 4, packet);
            LOG_DATA(count);
            uint32_t mask = XEGETUINT32BE(packet_base + 1 * 4);
            driver_->InvalidateState(mask);
          }
          break;

        default:
          XELOGGPU("[%.8X] Packet(%.8X): unknown!",
                   ptr + n * 4, packet);
          LOG_DATA(count);
          break;
        }

        n += 1 + count;
      }
      break;
    }
  }
}
