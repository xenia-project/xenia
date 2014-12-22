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

class GraphicsSystem {
 public:
  virtual ~GraphicsSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup();
  virtual void Shutdown();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  virtual void InitializeRingBuffer(uint32_t ptr, uint32_t page_count) = 0;
  virtual void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size) = 0;

  void DispatchInterruptCallback(uint32_t source, uint32_t cpu);

 protected:
  GraphicsSystem(Emulator* emulator);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;

  uint32_t interrupt_callback_;
  uint32_t interrupt_callback_data_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
