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
#include <memory>
#include <thread>

#include "xenia/cpu/processor.h"
#include "xenia/memory.h"
#include "xenia/ui/loop.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

namespace xe {
class Emulator;
}  // namespace xe

namespace xe {
namespace gpu {

class GraphicsSystem {
 public:
  virtual ~GraphicsSystem();

  static std::unique_ptr<GraphicsSystem> Create(Emulator* emulator);
  virtual std::unique_ptr<ui::GraphicsContext> CreateContext(
      ui::Window* target_window) = 0;

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }
  cpu::Processor* processor() const { return processor_; }

  virtual X_STATUS Setup(cpu::Processor* processor, ui::Loop* target_loop,
                         ui::Window* target_window);
  virtual void Shutdown();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  virtual void InitializeRingBuffer(uint32_t ptr, uint32_t page_count) = 0;
  virtual void EnableReadPointerWriteBack(uint32_t ptr,
                                          uint32_t block_size) = 0;

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

  Emulator* emulator_ = nullptr;
  Memory* memory_ = nullptr;
  cpu::Processor* processor_ = nullptr;
  ui::Loop* target_loop_ = nullptr;
  ui::Window* target_window_ = nullptr;

  uint32_t interrupt_callback_ = 0;
  uint32_t interrupt_callback_data_ = 0;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GRAPHICS_SYSTEM_H_
