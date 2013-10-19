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
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe


namespace xe {
namespace gpu {

class GraphicsDriver;
class RingBufferWorker;


class CreationParams {
public:
  xe_memory_ref     memory;

  CreationParams() :
      memory(0) {
  }
};


class GraphicsSystem {
public:
  virtual ~GraphicsSystem();

  xe_memory_ref memory();
  shared_ptr<cpu::Processor> processor();
  void set_processor(shared_ptr<cpu::Processor> processor);

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  virtual uint64_t ReadRegister(uint32_t r);
  virtual void WriteRegister(uint32_t r, uint64_t value);

  void DispatchInterruptCallback(uint32_t cpu = 0xFFFFFFFF);
  bool swap_pending() const { return swap_pending_; }
  void set_swap_pending(bool value) { swap_pending_ = value; }

public:
  // TODO(benvanik): have an HasRegisterHandler() so that the JIT can
  //                 just poke the register file directly.
  static uint64_t ReadRegisterThunk(GraphicsSystem* this_ptr, uint32_t r) {
    return this_ptr->ReadRegister(r);
  }
  static void WriteRegisterThunk(GraphicsSystem* this_ptr, uint32_t r,
                                 uint64_t value) {
    this_ptr->WriteRegister(r, value);
  }

protected:
  virtual void Initialize();
  virtual void Pump() = 0;
  virtual void Shutdown();

private:
  static void ThreadStartThunk(GraphicsSystem* this_ptr) {
    this_ptr->ThreadStart();
  }
  void ThreadStart();

protected:
  GraphicsSystem(const CreationParams* params);

  xe_memory_ref     memory_;
  shared_ptr<cpu::Processor> processor_;
  
  xe_run_loop_ref   run_loop_;
  xe_thread_ref     thread_;
  bool              running_;

  GraphicsDriver*   driver_;
  RingBufferWorker* worker_;

  uint32_t          interrupt_callback_;
  uint32_t          interrupt_callback_data_;
  double            last_interrupt_time_;
  bool              swap_pending_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
