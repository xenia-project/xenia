/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/ring_buffer_worker.h>


using namespace xe;
using namespace xe::gpu;


RingBufferWorker::RingBufferWorker(xe_memory_ref memory) :
    memory_(memory) {
  running_ = true;
  read_ptr_index_event_ = CreateEvent(
      NULL, FALSE, FALSE, NULL);
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

    #define READ_UINT32() \
        XEGETUINT32BE(p + primary_buffer_ptr_ + read_ptr_index_ * 4); \
        read_ptr_index_ = (read_ptr_index_ + 1) % (primary_buffer_size_ / 4);

    while (true) {
      uint32_t command = READ_UINT32();

      switch (command) {
      case 0xC0114800:
        {
          // Init packet.
          // Will have 18-19 ops after it. Maybe.
          XELOGGPU("Command(%.8X): init packet", command);
          for (int n = 0; n < 18; n++) {
            READ_UINT32();
          }
        }
        break;
      case 0xC0013F00:
        {
          // Kick segment.
          uint32_t segment_ptr = READ_UINT32();
          uint32_t length = READ_UINT32();
          XELOGGPU("Command(%.8X): kick segment %.8X (%db)",
                   command, segment_ptr, length * 4);
          ExecuteSegment(segment_ptr, length);
        }
        break;
      default:
        XELOGGPU("Command(%.8X): unknown primary buffer command", command);
        break;
      }

      if (read_ptr_index_ == write_ptr_index_) {
        break;
      }
    }

    // TODO(benvanik): use read_ptr_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      XESETUINT32BE(p + read_ptr_writeback_ptr_, read_ptr_index_);
    }
    SetEvent(read_ptr_index_event_);
  }
}

void RingBufferWorker::ExecuteSegment(uint32_t ptr, uint32_t length) {
  uint8_t* p = xe_memory_addr(memory_);

  // Adjust pointer base.
  ptr += (primary_buffer_ptr_ & ~0x1FFFFFFF);

  XELOGGPU("CommandList(%.8X): executing %d commands", ptr, length);

  // Execute commands!
  for (uint32_t n = 0; n < length; n++) {
    uint32_t command = XEGETUINT32BE(p + ptr + n * 4);
    XELOGGPU("  Command(%.8X)", command);
  }
}