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
#include <xenia/xbox.h>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, cpu, Processor);


namespace xe {
namespace gpu {

class GraphicsDriver;
class RingBufferWorker;


class GraphicsSystem {
public:
  virtual ~GraphicsSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  bool HandlesRegister(uint64_t addr);
  virtual uint64_t ReadRegister(uint64_t addr);
  virtual void WriteRegister(uint64_t addr, uint64_t value);

  void MarkVblank();
  void DispatchInterruptCallback(uint32_t source, uint32_t cpu = 0xFFFFFFFF);
  bool swap_pending() const { return swap_pending_; }
  void set_swap_pending(bool value) { swap_pending_ = value; }

protected:
  virtual void Initialize();
  virtual void Pump() = 0;
  virtual void Shutdown();

private:
  static void ThreadStartThunk(GraphicsSystem* this_ptr) {
    this_ptr->ThreadStart();
  }
  void ThreadStart();

  static bool HandlesRegisterThunk(GraphicsSystem* gs, uint64_t addr) {
    return gs->HandlesRegister(addr);
  }
  static uint64_t ReadRegisterThunk(GraphicsSystem* gs, uint64_t addr) {
    return gs->ReadRegister(addr);
  }
  static void WriteRegisterThunk(GraphicsSystem* gs, uint64_t addr,
                                 uint64_t value) {
    gs->WriteRegister(addr, value);
  }

protected:
  GraphicsSystem(Emulator* emulator);

  Emulator*         emulator_;
  Memory*           memory_;
  cpu::Processor*   processor_;

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
