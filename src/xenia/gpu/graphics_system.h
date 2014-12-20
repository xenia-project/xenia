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

#include <atomic>
#include <thread>

#include <xenia/common.h>
#include <xenia/emulator.h>
#include <xenia/xbox.h>

namespace xe {
namespace gpu {

class CommandProcessor;
class GraphicsDriver;

class GraphicsSystem {
 public:
  virtual ~GraphicsSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();
  virtual void Shutdown();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  virtual uint64_t ReadRegister(uint64_t addr);
  virtual void WriteRegister(uint64_t addr, uint64_t value);

  void MarkVblank();
  void DispatchInterruptCallback(uint32_t source, uint32_t cpu = 0xFFFFFFFF);
  virtual void Swap() = 0;

 protected:
  virtual void Initialize();
  virtual void Pump() = 0;

 private:
  void ThreadStart();

  static uint64_t MMIOReadRegisterThunk(GraphicsSystem* gs, uint64_t addr) {
    return gs->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(GraphicsSystem* gs, uint64_t addr,
                                     uint64_t value) {
    gs->WriteRegister(addr, value);
  }

 protected:
  GraphicsSystem(Emulator* emulator);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;

  xe_run_loop_ref run_loop_;
  std::thread thread_;
  std::atomic<bool> running_;

  GraphicsDriver* driver_;
  CommandProcessor* command_processor_;

  uint32_t interrupt_callback_;
  uint32_t interrupt_callback_data_;
  HANDLE thread_wait_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
