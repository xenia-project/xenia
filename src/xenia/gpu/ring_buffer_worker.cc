/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/ring_buffer_worker.h>

#include <xenia/gpu/xenos/packets.h>
#include <xenia/gpu/xenos/registers.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


RingBufferWorker::RingBufferWorker(xe_memory_ref memory) :
    memory_(memory) {
  running_ = true;
  write_ptr_index_event_ = CreateEvent(
      NULL, FALSE, FALSE, NULL);

  thread_ = xe_thread_create(
      "RingBufferWorker",
      (xe_thread_callback)ThreadStartThunk, this);
}

RingBufferWorker::~RingBufferWorker() {
  // TODO(benvanik): thread join.
  running_ = false;
  SetEvent(write_ptr_index_event_);
  xe_thread_release(thread_);
  CloseHandle(write_ptr_index_event_);
}

void RingBufferWorker::Initialize(uint32_t ptr, uint32_t page_count) {
  primary_buffer_ptr_ = ptr;
  primary_buffer_size_ = page_count * 4 * 1024;
  read_ptr_index_ = 0;

  xe_thread_start(thread_);
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

void RingBufferWorker::ThreadStart() {
  uint8_t* p = xe_memory_addr(memory_);

  while (running_) {
    if (write_ptr_index_ == 0xBAADF00D ||
        read_ptr_index_ == write_ptr_index_) {
      // Wait for the command buffer pointer to move.
      // TODO(benvanik): only wait for a bit and check running_.
      WaitForSingleObject(write_ptr_index_event_, INFINITE);
      if (!running_) {
        break;
      }
    }
    if (read_ptr_index_ == write_ptr_index_) {
      continue;
    }

    // Process the new commands.
    XELOGGPU("Ring buffer thread work");

    // TODO(benvanik): handle wrapping around
    // read_ptr_index_ = (read_ptr_index_ + 1) % (primary_buffer_size_ / 4);
    XEASSERT(write_ptr_index_ >= read_ptr_index_);
    uint32_t length = write_ptr_index_ - read_ptr_index_;
    if (length) {
      ExecuteSegment(primary_buffer_ptr_ + read_ptr_index_ * 4, length);
      read_ptr_index_ = write_ptr_index_;
    }

    // TODO(benvanik): use read_ptr_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      XESETUINT32BE(p + read_ptr_writeback_ptr_, read_ptr_index_);
    }
  }
}

void RingBufferWorker::ExecuteSegment(uint32_t ptr, uint32_t length) {
  uint8_t* p = xe_memory_addr(memory_);

  // Adjust pointer base.
  ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (ptr & 0x1FFFFFFF);

#define LOG_DATA(count) \
  for (uint32_t __m = 0; __m < count; __m++) { \
    XELOGGPU("  %.8X", XEGETUINT32BE(packet_base + 1 * 4 + __m * 4)); \
  }

  XELOGGPU("CommandList(%.8X): executing %dw", ptr, length);

  // Execute commands!
  for (uint32_t n = 0; n < length;) {
    const uint8_t* packet_base = p + ptr + n * 4;
    const uint32_t packet = XEGETUINT32BE(packet_base);
    const uint32_t packet_type = packet >> 30;
    switch (packet_type) {
    case 0x00:
      {
        // Type-0 packet.
        // Write count registers in sequence to the registers starting at
        // (base_index << 2).
        XELOGGPU("Packet(%.8X): set registers:", packet);
        uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
        uint32_t base_index = (packet & 0xFFFF);
        for (uint32_t m = 0; m < count; m++) {
          uint32_t reg_data = XEGETUINT32BE(packet_base + 1 * 4 + m * 4);
          const char* reg_name = xenos::GetRegisterName(base_index + m);
          XELOGGPU("  %.8X -> %.4X %s", reg_data, base_index + m,
                                        reg_name ? reg_name : "");
          // TODO(benvanik): process register writes.
        }
        n += 1 + count;
      }
      break;
    case 0x01:
      {
        // Type-1 packet.
        // Contains two registers of data. Type-0 should be more common.
        XELOGGPU("Packet(%.8X): set registers:", packet);
        uint32_t reg_index_1 = packet & 0x7FF;
        uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
        uint32_t reg_data_1 = XEGETUINT32BE(packet_base + 1 * 4);
        uint32_t reg_data_2 = XEGETUINT32BE(packet_base + 2 * 4);
        const char* reg_name_1 = xenos::GetRegisterName(reg_index_1);
        const char* reg_name_2 = xenos::GetRegisterName(reg_index_2);
        XELOGGPU("  %.8X -> %.4X %s", reg_data_1, reg_index_1,
                                      reg_name_1 ? reg_name_1 : "");
        XELOGGPU("  %.8X -> %.4X %s", reg_data_2, reg_index_2,
                                      reg_name_2 ? reg_name_2 : "");
        // TODO(benvanik): process register writes.
        n += 1 + 2;
      }
      break;
    case 0x02:
      // Type-2 packet.
      // No-op. Do nothing.
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
          XELOGGPU("Packet(%.8X): PM4_ME_INIT", packet);
          LOG_DATA(count);
          break;

        case PM4_NOP:
          // skip N 32-bit words to get to the next packet
          // No-op, ignore some data.
          XELOGGPU("Packet(%.8X): PM4_NOP", packet);
          LOG_DATA(count);
          break;

        case PM4_INDIRECT_BUFFER:
          // indirect buffer dispatch
          {
            uint32_t list_ptr = XEGETUINT32BE(packet_base + 1 * 4);
            uint32_t list_length = XEGETUINT32BE(packet_base + 2 * 4);
            XELOGGPU("Packet(%.8X): PM4_INDIRECT_BUFFER %.8X (%dw)",
                     packet, list_ptr, list_length);
            ExecuteSegment(list_ptr, list_length);
          }
          break;

        case PM4_WAIT_REG_MEM:
          // wait until a register or memory location is a specific value
          XELOGGPU("Packet(%.8X): PM4_WAIT_REG_MEM", packet);
          LOG_DATA(count);
          break;

        case PM4_REG_RMW:
          // register read/modify/write
          // ? (used during shader upload and edram setup)
          XELOGGPU("Packet(%.8X): PM4_REG_RMW", packet);
          LOG_DATA(count);
          break;

        case PM4_EVENT_WRITE_SHD:
          // generate a VS|PS_done event
          {
            XELOGGPU("Packet(%.8X): PM4_EVENT_WRITE_SHD", packet);
            LOG_DATA(count);
            // 3?
            uint32_t d0 = XEGETUINT32BE(packet_base + 1 * 4);
            // ptr
            uint32_t d1 = XEGETUINT32BE(packet_base + 2 * 4);
            // value?
            uint32_t d2 = XEGETUINT32BE(packet_base + 3 * 4);
            XESETUINT32BE(
                p + d1 + (primary_buffer_ptr_ & ~0x1FFFFFFF), d2);
          }
          break;

        case PM4_DRAW_INDX_2:
          // draw using supplied indices in packet
          XELOGGPU("Packet(%.8X): PM4_DRAW_INDX_2", packet);
          LOG_DATA(count);
          break;

        case PM4_IM_LOAD_IMMEDIATE:
          // load sequencer instruction memory (code embedded in packet)
          XELOGGPU("Packet(%.8X): PM4_IM_LOAD_IMMEDIATE", packet);
          LOG_DATA(count);
          break;
        case PM4_INVALIDATE_STATE:
          // selective invalidation of state pointers
          XELOGGPU("Packet(%.8X): PM4_INVALIDATE_STATE", packet);
          LOG_DATA(count);
          break;

        default:
          XELOGGPU("Packet(%.8X): unknown!", packet);
          LOG_DATA(count);
          break;
        }

        n += 1 + count;
      }
      break;
    }
  }
}
