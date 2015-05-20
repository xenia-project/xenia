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

#include "xenia/cpu/processor.h"
#include "xenia/memory.h"
#include "xenia/ui/main_window.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {

class GraphicsSystem {
 public:
  virtual ~GraphicsSystem();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup(cpu::Processor* processor,
                         ui::PlatformLoop* target_loop,
                         ui::PlatformWindow* target_window);
  virtual void Shutdown();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  virtual void InitializeRingBuffer(uint32_t ptr, uint32_t page_count) = 0;
  virtual void EnableReadPointerWriteBack(uint32_t ptr,
                                          uint32_t block_size) = 0;

  virtual void RequestSwap() = 0;

  void DispatchInterruptCallback(uint32_t source, uint32_t cpu);

  virtual void RequestFrameTrace() {}
  virtual void BeginTracing() {}
  virtual void EndTracing() {}
  enum class TracePlaybackMode {
    kUntilEnd,
    kBreakOnSwap,
  };
  virtual void PlayTrace(const uint8_t* trace_data, size_t trace_size,
                         TracePlaybackMode playback_mode) {}
  virtual void ClearCaches() {}

 protected:
  GraphicsSystem(Emulator* emulator);

  Emulator* emulator_;
  Memory* memory_;
  cpu::Processor* processor_;
  ui::PlatformLoop* target_loop_;
  ui::PlatformWindow* target_window_;

  uint32_t interrupt_callback_;
  uint32_t interrupt_callback_data_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
