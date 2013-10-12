/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_RING_BUFFER_WORKER_H_
#define XENIA_GPU_RING_BUFFER_WORKER_H_

#include <xenia/core.h>

#include <xenia/gpu/xenos/registers.h>


namespace xe {
namespace gpu {

class GraphicsDriver;

class RingBufferWorker {
public:
  RingBufferWorker(xe_memory_ref memory);
  virtual ~RingBufferWorker();

  xe_memory_ref memory();

  void Initialize(GraphicsDriver* driver,
                  uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  void UpdateWritePointer(uint32_t value);

  void Pump();

protected:
  void ExecuteSegment(uint32_t ptr, uint32_t length);

protected:
  xe_memory_ref   memory_;

  GraphicsDriver* driver_;

  uint32_t        primary_buffer_ptr_;
  uint32_t        primary_buffer_size_;

  uint32_t        read_ptr_index_;
  uint32_t        read_ptr_update_freq_;
  uint32_t        read_ptr_writeback_ptr_;

  HANDLE          write_ptr_index_event_;
  uint32_t        write_ptr_index_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_RING_BUFFER_WORKER_H_
