/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_GRAPHICS_SYSTEM_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {

class RingBufferWorker;


class CreationParams {
public:
  xe_memory_ref memory;

  CreationParams() : memory(NULL) {}
};


class GraphicsSystem {
public:
  virtual ~GraphicsSystem();

  xe_memory_ref memory();

  virtual void Initialize() = 0;
  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  virtual uint64_t ReadRegister(uint32_t r);
  virtual void WriteRegister(uint32_t r, uint64_t value);

public:
  static uint64_t ReadRegisterThunk(GraphicsSystem* this_ptr, uint32_t r) {
    return this_ptr->ReadRegister(r);
  }
  static void WriteRegisterThunk(GraphicsSystem* this_ptr, uint32_t r, uint64_t value) {
    this_ptr->WriteRegister(r, value);
  }

protected:
  GraphicsSystem(const CreationParams* params);

  xe_memory_ref     memory_;

  RingBufferWorker* worker_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
